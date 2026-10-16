// emote_preview.cpp — dev tool (ARTC_TEST_CGL): render one E-mote model to a
// PNG on a macOS offscreen CGL context, using the same player/container
// transforms the game passes for a real 立绘 layer. Used to inspect layer
// order / masking locally without a device round-trip.
#include "render/emote_model.h"
#include "render/emote_player.h"
#include "render/emote_scene.h"
#include "render/compositor.h"
#include "render/gles2_headers.h"
#include "pack/psb.h"
#include "util/snapshot_image.h"
#include <OpenGL/OpenGL.h>
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
bool Number(const std::string& text, double& value) {
    char* end = nullptr;
    errno = 0;
    value = std::strtod(text.c_str(), &end);
    return !text.empty() && text.find_first_of(" \t\r\n\f\v") == std::string::npos &&
           end == text.c_str() + text.size() && errno != ERANGE && std::isfinite(value);
}

std::string Json(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
        else out << c;
    }
    out << '"';
    return out.str();
}

std::string Sha256(const std::vector<uint8_t>& bytes) {
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = static_cast<CC_LONG>(std::min(bytes.size() - offset,
                                            size_t(std::numeric_limits<CC_LONG>::max())));
        CC_SHA256_Update(&context, bytes.data() + offset, count);
        offset += count;
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    std::ostringstream out;
    for (unsigned char c : digest) out << std::hex << std::setw(2) << std::setfill('0') << int(c);
    return out.str();
}

bool WritePrivate(const std::string& path, const void* data, size_t size) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        std::cerr << "cannot create new output " << path << ": " << std::strerror(errno) << '\n';
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    while (offset < size) {
        const ssize_t count = ::write(fd, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            std::cerr << "write failed: " << path << ": " << std::strerror(errno) << '\n';
            ::close(fd);
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    if (::close(fd)) { std::cerr << "close failed: " << path << '\n'; return false; }
    return true;
}

bool MakeCurrent() {
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAAccelerated, kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
        kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8, (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pix = nullptr; GLint npix = 0;
    if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix) return false;
    CGLContextObj ctx = nullptr;
    const CGLError result = CGLCreateContext(pix, nullptr, &ctx);
    CGLReleasePixelFormat(pix);
    if (result || !ctx) return false;
    return CGLSetCurrentContext(ctx) == kCGLNoError;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[1], "--help") == 0) {
        std::cerr << "usage: emote_preview <model.psb> <out.png> [frame | --frame N]\n"
                     "  [--variable label=value]... [--timeline label]... [--manifest path]\n"
                     "Frame must be finite and in [0, 216000] (60 fps; max one hour). Variables are set at t=0;\n"
                     "timelines play in parallel before progression. Duplicate variables apply in order.\n"
                     "Legacy dev transforms remain unchanged; this is not an official reference state.\n"
                     "PNG and JSON (default out.png.manifest.json) must be new private files.\n"
                     "Use tools/emote_validation.py for unique directories and source/build provenance.\n";
        return argc >= 2 && std::strcmp(argv[1], "--help") == 0 ? 0 : 2;
    }
    double frame = 0;
    bool frame_set = false;
    std::string manifest_path = std::string(argv[2]) + ".manifest.json";
    bool manifest_set = false;
    std::vector<std::pair<std::string, double>> variables;
    std::vector<std::string> timelines;
    auto bad = [](const std::string& text) { std::cerr << "emote_preview: " << text << '\n'; return 2; };
    for (int i = 3; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--frame" || option == "--variable" || option == "--timeline" || option == "--manifest") {
            if (i + 1 >= argc || !*argv[i + 1] || std::strncmp(argv[i + 1], "--", 2) == 0)
                return bad("missing value for " + option);
            const std::string value = argv[++i];
            if (option == "--frame") {
                if (frame_set || !Number(value, frame) || frame < 0) return bad("invalid/duplicate frame: " + value);
                frame_set = true;
            } else if (option == "--variable") {
                const auto equals = value.find('=');
                double number = 0;
                if (equals == std::string::npos || equals == 0 || !Number(value.substr(equals + 1), number))
                    return bad("invalid variable (expected label=finite-number): " + value);
                variables.emplace_back(value.substr(0, equals), number);
            } else if (option == "--timeline") {
                timelines.push_back(value);
            } else {
                if (manifest_set) return bad("duplicate --manifest");
                manifest_path = value;
                manifest_set = true;
            }
        } else {
            if (frame_set || !Number(option, frame) || frame < 0) return bad("unknown option or invalid frame: " + option);
            frame_set = true;
        }
    }
    // Check the derived time too: a finite frame may overflow milliseconds.
    const double elapsed_ms = frame * 1000.0 / 60.0;
    if (!std::isfinite(elapsed_ms)) return bad("frame overflows elapsed milliseconds");
    if (frame > 216000) return bad("frame exceeds diagnostic limit of 216000 (one hour)");
    if (!*argv[1] || !*argv[2] || manifest_path.empty()) return bad("paths must not be empty");
    for (const auto& path : {std::string(argv[2]), manifest_path}) {
        struct stat status;
        if (::lstat(path.c_str(), &status) == 0 || errno != ENOENT)
            return bad("output exists or is inaccessible: " + path);
    }
    if (manifest_path == argv[2]) return bad("PNG and manifest paths must differ");
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::cerr << "cannot open model: " << argv[1] << '\n'; return 1; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    if (f.bad()) { std::cerr << "model read failed\n"; return 1; }
    const std::string model_sha256 = Sha256(bytes);
    artc::PsbDocument document;
    std::string error;
    if (!artc::DecodePsb(bytes, document, error)) {
        std::cerr << "decode: " << error << '\n';
        return 1;
    }
    auto model = std::make_shared<artc::EmoteModel>();
    if (!model->Load(std::move(document), error)) {
        std::cerr << "model: " << error << '\n';
        return 1;
    }
    if (!MakeCurrent()) {
        std::cerr << "no GL context\n";
        return 1;
    }
    const int W = 1920, H = 1080;
    // Drawable-less CGL has no default framebuffer; draw into an offscreen FBO.
    GLuint screen_tex = 0, screen_fbo = 0;
    glGenTextures(1, &screen_tex);
    glBindTexture(GL_TEXTURE_2D, screen_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &screen_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, screen_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screen_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "incomplete screen FBO\n";
        return 1;
    }
    artc::Compositor compositor;
    compositor.Init(W, H);
    artc::EmotePlayer player;
    if (!player.Load(model, error)) {
        std::cerr << "player: " << error << '\n';
        return 1;
    }
    for (const auto& variable : variables) {
        if (!player.SetVariable(variable.first, variable.second, 0, 0, error)) {
            std::cerr << "variable " << variable.first << ": " << error << '\n';
            return 1;
        }
    }
    for (const auto& label : timelines) {
        if (!player.PlayTimeline(label, artc::EmotePlayer::kTimelineParallel, error)) {
            std::cerr << "timeline " << label << ": " << error << '\n';
            return 1;
        }
    }
    // Device container transform for 甜蜜女友3 fg 立绘: box 1920x2048, setCoord
    // and setScale from ex.lyc, game side top=-484/anchor(960,540)/scale 86.
    player.SetLayerSize(1920, 2048);
    player.SetCoordAngle(-64, 309, 0, 0);
    player.SetScaleOrigin(0.5, 0, 0);
    // Progress clamps one call; step in 16 ms ticks like the frame loop.
    for (double remaining = elapsed_ms; remaining > 0; remaining -= 16.0)
        player.Progress(std::min(16.0, remaining));
    std::map<std::string, double> filter_variables;
    for (int i = 0; i < player.CountVariables(); ++i) {
        const auto label = player.VariableLabelAt(i);
        bool found = false;
        filter_variables[label] = player.GetVariable(label, &found);
    }
    std::cerr << "base_frame=" << player.BaseFrame() << '\n';
    compositor.SetProps("g", {{"left", "0"}, {"top", "-484"}, {"anchorx", "960"},
                              {"anchory", "540"}, {"xscale", "86"}, {"yscale", "86"}});
    if (!player.Render(compositor, "g.0", error)) {
        std::cerr << "render: " << error << '\n';
        return 1;
    }
    // Dev filter: only=<label substring> hides every other part so a single
    // layer's placement/shape can be inspected in isolation.
    if (const char *only = std::getenv("EMOTE_PREVIEW_ONLY")) {
        std::vector<std::string> keep;
        artc::EmoteScene scene;
        std::string scene_error;
        if (scene.Load(model, scene_error)) {
            std::vector<artc::EmoteSceneLayer> evaluated;
            if (!scene.Evaluate(player.BaseFrame(), filter_variables, evaluated, scene_error)) {
                std::cerr << "filter evaluation: " << scene_error << '\n'; return 1;
            }
            for (const auto &l : evaluated)
                if (!l.source.empty() && l.label.find(only) != std::string::npos)
                    keep.push_back("g.0" + l.key + ".000000");
        } else {
            std::cerr << "filter load: " << scene_error << '\n'; return 1;
        }
        for (const auto &l : compositor.Layers()) {
            if (l.id.size() < 7 || l.id.compare(l.id.size() - 7, 7, ".000000") != 0)
                continue;
            if (std::find(keep.begin(), keep.end(), l.id) == keep.end())
                compositor.SetProps(l.id, {{"visible", "0"}});
        }
    }
    if (const char *hide = std::getenv("EMOTE_PREVIEW_HIDE")) {
        std::vector<std::string> drop;
        artc::EmoteScene scene;
        std::string scene_error;
        if (scene.Load(model, scene_error)) {
            std::vector<artc::EmoteSceneLayer> evaluated;
            if (!scene.Evaluate(player.BaseFrame(), filter_variables, evaluated, scene_error)) {
                std::cerr << "filter evaluation: " << scene_error << '\n'; return 1;
            }
            for (const auto &l : evaluated)
                if (!l.source.empty() && l.label.find(hide) != std::string::npos)
                    drop.push_back("g.0" + l.key + ".000000");
        } else {
            std::cerr << "filter load: " << scene_error << '\n'; return 1;
        }
        for (const auto &l : compositor.Layers())
            if (std::find(drop.begin(), drop.end(), l.id) != drop.end())
                compositor.SetProps(l.id, {{"visible", "0"}});
    }
    // Dev experiment: ids without a leading number sort to the bottom; this
    // moves matching parts behind every numeric layer.
    if (const char *back = std::getenv("EMOTE_PREVIEW_BACK")) {
        std::vector<std::string> move;
        artc::EmoteScene scene;
        std::string scene_error;
        if (scene.Load(model, scene_error)) {
            std::vector<artc::EmoteSceneLayer> evaluated;
            if (!scene.Evaluate(player.BaseFrame(), filter_variables, evaluated, scene_error)) {
                std::cerr << "filter evaluation: " << scene_error << '\n'; return 1;
            }
            for (const auto &l : evaluated)
                if (!l.source.empty() && l.label.find(back) != std::string::npos)
                    move.push_back("g.0" + l.key + ".000000");
        } else {
            std::cerr << "filter load: " << scene_error << '\n'; return 1;
        }
        int index = 0;
        for (const auto &l : compositor.Layers())
            for (const auto &id : move)
                if (l.id == id)
                    compositor.RenameLayer(l.id, "zzback" + std::to_string(index++));
    }
    // InitGl may have rebound FBO 0; target the offscreen surface again.
    glBindFramebuffer(GL_FRAMEBUFFER, screen_fbo);
    glViewport(0, 0, W, H);
    compositor.Draw();
    artc::SnapshotImage image;
    if (!compositor.Snapshot(image) || image.rgba.size() != size_t(W) * H * 4) {
        std::cerr << "snapshot unavailable\n";
        return 1;
    }
    std::vector<uint8_t> png;
    if (!image.EncodePng(W, H, png)) {
        std::cerr << "encode failed\n";
        return 1;
    }
    std::ostringstream manifest;
    manifest << std::setprecision(17)
             << "{\n  \"schema\": \"artc.emote-preview.v1\",\n"
             << "  \"reference_state\": false,\n  \"profile\": \"legacy-dev-preview\",\n"
             << "  \"model_path\": " << Json(argv[1]) << ",\n  \"model_sha256\": " << Json(model_sha256)
             << ",\n  \"output_path\": " << Json(argv[2])
             << ",\n  \"requested_frame\": " << frame << ",\n  \"elapsed_ms\": " << elapsed_ms
             << ",\n  \"base_frame\": " << player.BaseFrame()
             << ",\n  \"fps\": 60,\n  \"progress_tick_ms\": 16,\n"
             << "  \"build\": {\"compiled\": " << Json(__DATE__ " " __TIME__)
             << ", \"compiler\": " << Json(__clang_version__) << ", \"backend\": \"CGL\"},\n"
             << "  \"source_state\": \"not embedded; use validation wrapper for working-tree provenance\",\n"
             << "  \"layer_size\": [1920, 2048],\n  \"coord\": [-64, 309, 0, 0],\n"
             << "  \"scale_origin\": [0.5, 0, 0],\n"
             << "  \"container\": {\"top\": -484, \"anchor\": [960, 540], \"scale\": [86, 86]},\n"
             << "  \"variable_requests\": [";
    for (size_t i = 0; i < variables.size(); ++i) {
        if (i) manifest << ',';
        manifest << "{\"label\":" << Json(variables[i].first) << ",\"value\":" << variables[i].second << '}';
    }
    manifest << "],\n  \"player_variables\": {";
    bool first = true;
    for (const auto& variable : filter_variables) {
        if (!first) manifest << ',';
        first = false;
        manifest << Json(variable.first) << ':' << variable.second;
    }
    manifest << "},\n  \"timeline_requests\": [";
    for (size_t i = 0; i < timelines.size(); ++i) {
        if (i) manifest << ',';
        manifest << Json(timelines[i]);
    }
    manifest << "],\n  \"filters\": {";
    first = true;
    for (const char* name : {"EMOTE_PREVIEW_ONLY", "EMOTE_PREVIEW_HIDE", "EMOTE_PREVIEW_BACK"}) {
        if (!first) manifest << ',';
        first = false;
        const char* value = std::getenv(name);
        manifest << Json(name) << ':' << (value ? Json(value) : "null");
    }
    manifest << "},\n  \"filter_note\": \"diagnostic label selection; not composed timeline/selector state\"\n}\n";
    const std::string json = manifest.str();
    if (!WritePrivate(argv[2], png.data(), png.size()) ||
        !WritePrivate(manifest_path, json.data(), json.size())) return 1;
    std::cout << "wrote " << argv[2] << " frame=" << frame << '\n';
    return 0;
}
