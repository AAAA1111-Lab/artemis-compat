// Synthetic PSB v2/v3/v4 encryption and strict independent-block LZ4 frames.
// No game assets, title keys or official binaries are used.

#include "pack/psb.h"
#include "emote_scene_fixture.h"

#include <cstdlib>
#include <limits>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

int g_failures = 0;
void Check(bool ok, const std::string &what) {
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
}

constexpr uint32_t kKey1 = 123456789u;
constexpr uint32_t kKey2 = 362436069u;
constexpr uint32_t kKey3 = 521288629u;

struct Cipher {
    uint32_t key1 = kKey1, key2 = kKey2, key3 = kKey3, key4, current = 0;
    explicit Cipher(uint32_t seed) : key4(seed) {}
    void Apply(uint8_t *data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (current == 0) {
                const uint32_t a = key1 ^ (key1 << 11), b = key4;
                const uint32_t next = a ^ b ^ ((a ^ (b >> 11)) >> 8);
                key1 = key2; key2 = key3; key3 = b; key4 = next; current = next;
            }
            data[i] ^= static_cast<uint8_t>(current);
            current >>= 8;
        }
    }
};

void Put32(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t Adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// Independent test writer from the published XXH32 algorithm description:
// https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md (v0.2.0).
// No reference implementation is included. Known-answer vectors below keep
// the frame writer from merely agreeing with a broken reader.
uint32_t FrameHash(const std::vector<uint8_t>& bytes) {
    constexpr uint32_t p1=0x9e3779b1u,p2=0x85ebca77u,p3=0xc2b2ae3du,
                       p4=0x27d4eb2fu,p5=0x165667b1u;
    auto rotate=[](uint32_t x,unsigned n){return (x<<n)|(x>>(32-n));};
    auto word=[&](size_t i) {
        uint32_t v=0;for(unsigned j=0;j<4;++j)v|=uint32_t(bytes[i+j])<<(8*j);return v;
    };
    size_t at=0;
    uint32_t hash=p5;
    if(bytes.size()>=16) {
        uint32_t lanes[4]={p1+p2,p2,0,0u-p1};
        while(bytes.size()-at>=16) {
            for(auto& lane:lanes){lane=rotate(lane+word(at)*p2,13)*p1;at+=4;}
        }
        hash=rotate(lanes[0],1)+rotate(lanes[1],7)+rotate(lanes[2],12)+rotate(lanes[3],18);
    }
    hash+=uint32_t(bytes.size());
    while(bytes.size()-at>=4){hash=rotate(hash+word(at)*p3,17)*p4;at+=4;}
    while(at<bytes.size())hash=rotate(hash+uint32_t(bytes[at++])*p5,11)*p1;
    hash=(hash^(hash>>15))*p2;hash=(hash^(hash>>13))*p3;
    return hash^(hash>>16);
}

uint32_t Get32(const std::vector<uint8_t>& b,size_t at) {
    return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|
           (uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);
}
void Append32(std::vector<uint8_t>& b,uint32_t v) {
    const size_t at=b.size();b.resize(at+4);Put32(b,at,v);
}

artc::PsbDocument SampleDocument();
void Rejected(const std::vector<uint8_t>& bytes,const std::string& what,
              const std::string& diagnostic="") {
    auto doc=SampleDocument();
    doc.version=99;doc.bytes={1,2,3};doc.resources={{1,2}};doc.extra_resources={{2,1}};
    const auto input=bytes;
    std::string error;
    const bool ok=artc::DecodePsb(bytes,doc,error);
    Check(!ok,what+" rejected");
    Check(!error.empty() && error.find(diagnostic)!=std::string::npos,what+" diagnostic: "+error);
    Check(doc.version==99 && doc.bytes==std::vector<uint8_t>({1,2,3}) &&
          doc.resources==std::vector<std::pair<size_t,size_t>>({{1,2}}) &&
          doc.extra_resources==std::vector<std::pair<size_t,size_t>>({{2,1}}) &&
          doc.root.At("answer").Num(-1)==42 && doc.root.At("text").string=="hello",
          what+" preserves previous document");
    Check(input==bytes,what+" preserves input");
}

// Sign the v3 header payload ([8,40)) into the checksum word at offset 40.
void SignV3Header(std::vector<uint8_t> &plain) { Put32(plain, 40, Adler32(plain.data() + 8, 32)); }

artc::PsbDocument SampleDocument() {
    artc::PsbDocument doc;
    doc.root.type = artc::PsbValue::Object;
    artc::PsbValue n; n.type = artc::PsbValue::Number; n.number = 42;
    doc.root.object["answer"] = n;
    artc::PsbValue s; s.type = artc::PsbValue::String; s.string = "hello";
    doc.root.object["text"] = s;
    return doc;
}

std::vector<uint8_t> EncryptedFixture(uint32_t seed) {
    const artc::PsbDocument doc = SampleDocument();
    std::vector<uint8_t> plain = emote_fixture::EncodePsb(doc);
    Put32(plain, 8, 44);       // header_length (anchor derived by the decoder)
    SignV3Header(plain);       // adler32 of [8,40) -> offset 40
    plain[6] = 1;              // encryption_flags
    plain[7] = 0;
    Cipher(seed).Apply(plain.data() + 8, 44 - 8);
    return plain;
}

void TestEncryptedHeaderRoundTrip() {
    std::vector<uint8_t> bytes = EncryptedFixture(0x0BADF00Du);
    artc::PsbDocument decoded;
    std::string error;
    Check(artc::DecodePsb(bytes, decoded, error), "decode encrypted header: " + error);
    Check(decoded.root.At("answer").Num(-1) == 42, "encrypted tree preserved (number)");
    Check(decoded.root.At("text").string == "hello", "encrypted tree preserved (string)");
}

void TestHostSeedHook() {
    const std::vector<uint8_t> bytes = EncryptedFixture(0x00C0FFEEu);
    artc::SetPsbDecryptSeed(0x00c0ffeeu);
    artc::PsbDocument decoded;
    std::string error;
    const bool ok = artc::DecodePsb(bytes, decoded, error);
    artc::SetPsbDecryptSeed(0);
    Check(ok, "host seed hook decodes: " + error);
    Check(decoded.root.At("answer").Num(-1) == 42, "host seed tree preserved");
}

// PSB v2: the header has no checksum and the body up to the chunk-offset table
// is encrypted with the same stream (reference host: emotefile::load).
std::vector<uint8_t> EncryptedV2Fixture(uint32_t seed) {
    const artc::PsbDocument doc = SampleDocument();
    std::vector<uint8_t> plain = emote_fixture::EncodePsb(doc);
    std::vector<uint8_t> v2(plain.begin(), plain.begin() + 40);
    v2.insert(v2.end(), plain.begin() + 44, plain.end());  // drop the v3 checksum slot
    auto rd32 = [&](size_t at) {
        return uint32_t(v2[at]) | (uint32_t(v2[at + 1]) << 8) |
               (uint32_t(v2[at + 2]) << 16) | (uint32_t(v2[at + 3]) << 24);
    };
    auto wr32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) v2[at + i] = uint8_t(v >> (8 * i));
    };
    for (size_t at : {12u, 16u, 20u, 24u, 28u, 32u, 36u}) {
        const uint32_t v = rd32(at);
        if (v >= 44) wr32(at, v - 4);
    }
    wr32(8, 40);          // offsetEncrypt = header length
    v2[4] = 2;            // version 2
    v2[6] = 1;            // encryption flag
    v2[7] = 0;
    const uint32_t body_end = rd32(24);
    std::vector<uint8_t> out = v2;
    Cipher cipher(seed);
    cipher.Apply(out.data() + 8, 40 - 8);
    cipher.Apply(out.data() + 40, body_end - 40);
    return out;
}

void TestEncryptedV2Body() {
    std::vector<uint8_t> bytes = EncryptedV2Fixture(0x0BADF00Du);
    artc::PsbDocument decoded;
    std::string error;
    Check(artc::DecodePsb(bytes, decoded, error), "decode encrypted v2 body: " + error);
    Check(decoded.root.At("answer").Num(-1) == 42, "v2 tree preserved (number)");
    Check(decoded.root.At("text").string == "hello", "v2 tree preserved (string)");
}

// Synthetic frame writer: checksums are real, including on intentionally
// unsupported descriptors, so tests cannot pass just by rejecting a bad HC.
std::vector<uint8_t> FrameHeader(uint8_t flags=0x60,uint8_t bd=0x70,uint64_t size=0) {
    std::vector<uint8_t> frame={0x04,0x22,0x4d,0x18,flags,bd};
    if(flags&8)for(unsigned i=0;i<8;++i)frame.push_back(uint8_t(size>>(8*i)));
    if(flags&1)Append32(frame,0); // even dictionary ID zero requires a dictionary contract
    const uint8_t hc=uint8_t(FrameHash({frame.begin()+4,frame.end()})>>8);
    frame.push_back(hc);return frame;
}
std::vector<uint8_t> LiteralBlock(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> block;
    if(payload.size()<15)block.push_back(uint8_t(payload.size()<<4));
    else {
        block.push_back(0xf0);size_t rest=payload.size()-15;
        while(rest>=255){block.push_back(255);rest-=255;}
        block.push_back(uint8_t(rest));
    }
    block.insert(block.end(),payload.begin(),payload.end());return block;
}
void FrameBlock(std::vector<uint8_t>& frame,const std::vector<uint8_t>& block,bool stored) {
    Append32(frame,uint32_t(block.size())|(stored?0x80000000u:0));
    frame.insert(frame.end(),block.begin(),block.end());
    if(frame[4]&0x10)Append32(frame,FrameHash(block));
}
void FrameEnd(std::vector<uint8_t>& frame,const std::vector<uint8_t>& payload) {
    Append32(frame,0);if(frame[4]&4)Append32(frame,FrameHash(payload));
}
std::vector<uint8_t> Lz4Frame(const std::vector<uint8_t>& payload,bool linked,bool compressed,
                              uint8_t checks=0,uint8_t bd=0x70) {
    auto frame=FrameHeader((linked?0x40:0x60)|checks,bd,payload.size());
    FrameBlock(frame,compressed?LiteralBlock(payload):payload,!compressed);
    FrameEnd(frame,payload);return frame;
}

void TestLz4Containers() {
    const artc::PsbDocument doc = SampleDocument();
    const std::vector<uint8_t> plain = emote_fixture::EncodePsb(doc);
    artc::PsbDocument decoded;
    std::string error;
    Check(artc::DecodePsb(Lz4Frame(plain, false, false), decoded, error),
          "lzfs stored block: " + error);
    Check(decoded.root.At("answer").Num(-1) == 42, "lzfs stored tree preserved");
    Check(artc::DecodePsb(Lz4Frame(plain, false, true), decoded, error),
          "lzfs compressed block: " + error);
    Check(decoded.root.At("answer").Num(-1) == 42, "lzfs compressed tree preserved");
    for(uint8_t checks:{0u,4u,8u,16u,28u})for(uint8_t bd:{0x40u,0x50u,0x60u,0x70u}) {
        for(bool compressed:{false,true}) {
            const auto frame=Lz4Frame(plain,false,compressed,checks,bd);
            error="stale error";
            const bool ok=artc::DecodePsb(frame,decoded,error);
            Check(ok && error.empty() && decoded.bytes==plain && decoded.root.At("answer").Num(-1)==42,
                  "LZ4 checks/BD/stored-compressed roundtrip: "+error);
        }
    }
    Rejected(Lz4Frame(plain,true,false),"linked blocks","linked");

    // Split independent blocks (one stored, one compressed), including a
    // legal empty stored block that still has its own block checksum.
    auto multiple=FrameHeader(0x7c,0x40,plain.size());
    FrameBlock(multiple,{},true);
    FrameBlock(multiple,{plain.begin(),plain.begin()+31},true);
    FrameBlock(multiple,LiteralBlock({plain.begin()+31,plain.end()}),false);
    FrameBlock(multiple,{},true);
    FrameEnd(multiple,plain);
    Check(artc::DecodePsb(multiple,decoded,error) && decoded.bytes==plain,"mixed independent blocks: "+error);

    // Exercise overlapping matches, match-length extension and a literal tail.
    // Trailing bytes are unused by PSB but included in every frame checksum.
    auto padded=plain;padded.insert(padded.end(),400,'x');
    auto matched=LiteralBlock(plain);
    matched[0]|=15;matched.insert(matched.end(),{1,0,255,121,0x50,'x','x','x','x','x'});
    // The match repeats the last PSB byte, not an arbitrary 'x'.
    for(size_t i=plain.size();i<plain.size()+395;++i)padded[i]=plain.back();
    auto frame=FrameHeader(0x7c,0x40,padded.size());
    FrameBlock(frame,matched,false);FrameEnd(frame,padded);
    Check(artc::DecodePsb(frame,decoded,error) && decoded.bytes==padded,"overlapping extended match: "+error);
    Check(artc::DecodePsb(Lz4Frame(EncryptedV2Fixture(0x1234abcdu),false,true,28),decoded,error) &&
          decoded.version==2 && decoded.root.At("text").string=="hello","LZ4 wrapped encrypted v2: "+error);
}

void TestFrameHashes() {
    const auto plain=emote_fixture::EncodePsb(SampleDocument());
    for(const auto& v:std::vector<std::pair<std::string,uint32_t>>{
            {"",0x02cc5d05u},{"a",0x550d7456u},{"abc",0x32d153ffu},
            {"message digest",0x7c948494u},{"abcdefghijklmnopqrstuvwxyz",0x63a14d5fu}}) {
        Check(FrameHash({v.first.begin(),v.first.end()})==v.second,"XXH32 known answer: "+v.first);
        // Feed the hard-coded known answer through the production block hash,
        // not just through our test frame writer's own implementation.
        auto frame=FrameHeader(0x70);FrameBlock(frame,plain,true);
        Append32(frame,uint32_t(v.first.size())|0x80000000u);
        frame.insert(frame.end(),v.first.begin(),v.first.end());Append32(frame,v.second);
        FrameEnd(frame,plain);
        auto expected=plain;expected.insert(expected.end(),v.first.begin(),v.first.end());
        artc::PsbDocument doc;std::string error;
        Check(artc::DecodePsb(frame,doc,error) && doc.bytes==expected,"reader XXH32 known answer: "+v.first+": "+error);
    }
    Check(FrameHeader()==std::vector<uint8_t>({4,0x22,0x4d,0x18,0x60,0x70,0x73}),
          "standard independent 4 MiB frame header checksum");
}

void TestBlockBoundaries() {
    const auto plain=emote_fixture::EncodePsb(SampleDocument());
    auto payload=plain;payload.resize(65536,plain.back());
    artc::PsbDocument doc;std::string error;
    Check(artc::DecodePsb(Lz4Frame(payload,false,false,28,0x40),doc,error) && doc.bytes==payload,
          "stored block exactly at BD maximum: "+error);
    auto compressed=LiteralBlock(plain);compressed[0]|=15;
    compressed.insert(compressed.end(),{1,0});
    size_t remaining=65536-plain.size()-5-19;
    while(remaining>=255){compressed.push_back(255);remaining-=255;}
    compressed.push_back(uint8_t(remaining));compressed.push_back(0x50);
    compressed.insert(compressed.end(),5,plain.back());
    auto frame=FrameHeader(0x7c,0x40,payload.size());
    FrameBlock(frame,compressed,false);FrameEnd(frame,payload);
    Check(artc::DecodePsb(frame,doc,error) && doc.bytes==payload,
          "compressed block expands exactly to BD maximum: "+error);
    frame=FrameHeader(0x7c,0x40,payload.size()-1);
    FrameBlock(frame,compressed,false);FrameEnd(frame,payload);
    Rejected(frame,"compressed block expands beyond declared content size","content size");

    // Empty compressed blocks are one zero token, not an EndMark. Tiny
    // literal-only blocks and short (non-extended) matches are valid too.
    frame=FrameHeader(0x70);FrameBlock(frame,plain,true);FrameBlock(frame,{0},false);
    FrameBlock(frame,{0x30,'a','b','c'},false);
    FrameBlock(frame,{0x14,'x',1,0,0x50,'x','x','x','x','x'},false);
    FrameEnd(frame,{});
    payload=plain;payload.insert(payload.end(),{'a','b','c'});payload.insert(payload.end(),14,'x');
    Check(artc::DecodePsb(frame,doc,error) && doc.bytes==payload,
          "empty/tiny literal blocks and short overlap match: "+error);
}

void TestBadLz4Frames() {
    const auto plain=emote_fixture::EncodePsb(SampleDocument());
    const auto good=Lz4Frame(plain,false,false,28);
    const size_t hc=14,block_data=19,block_hash=block_data+plain.size();
    auto bad=good;bad[hc]^=1;Rejected(bad,"header checksum corruption","header checksum");
    bad=good;bad[block_hash]^=1;Rejected(bad,"block checksum corruption","block checksum");
    bad=good;bad.back()^=1;Rejected(bad,"content checksum corruption","content checksum");
    bad=good;bad[block_data+7]^=1;Rejected(bad,"stored block corruption","block checksum");
    bad=Lz4Frame(plain,false,true,28);bad[bad.size()-9]^=1;
    Rejected(bad,"compressed block checksum corruption","block checksum");
    for(uint64_t size:{uint64_t(0),uint64_t(plain.size()-1),uint64_t(plain.size()+1),
                       uint64_t(256*1024*1024+1),std::numeric_limits<uint64_t>::max()}) {
        bad=FrameHeader(0x68,0x70,size);FrameBlock(bad,plain,true);FrameEnd(bad,plain);
        Rejected(bad,"content size mismatch/limit","content size");
    }
    for(uint8_t flags:{0x20u,0xa0u,0x62u,0x61u}) {
        bad=FrameHeader(flags);FrameBlock(bad,plain,true);FrameEnd(bad,plain);
        Rejected(bad,"unsupported/invalid flags "+std::to_string(flags),flags==0x61?"dictionary":"lzfs");
    }
    for(uint8_t bd:{0u,0x10u,0x20u,0x30u,0x80u,0x71u}) {
        bad=FrameHeader(0x60,bd);FrameBlock(bad,plain,true);FrameEnd(bad,plain);
        Rejected(bad,"invalid block descriptor "+std::to_string(bd),"descriptor");
    }
    // Every truncation, including optional fields and all three checksums.
    for(size_t i=0;i<good.size();++i)
        Rejected({good.begin(),good.begin()+i},"frame truncated at "+std::to_string(i));
    bad=good;bad.push_back(0);Rejected(bad,"trailing frame data","trailing");
    bad=good;bad.insert(bad.end(),good.begin(),good.end());
    Rejected(bad,"concatenated frames","trailing");
    bad=FrameHeader();Append32(bad,0xffffffffu);FrameEnd(bad,plain);
    Rejected(bad,"oversized encoded block","block");
    auto large=plain;large.resize(65537);
    Rejected(Lz4Frame(large,false,false,0,0x40),"stored block exceeds BD","block");
    Rejected(Lz4Frame(large,false,true,0,0x40),"compressed block exceeds BD","block");

    // A second independent block must not read history from the first. The
    // old reader accepted this and appended seven copies of the last byte.
    // The literal tail and last-match distance are otherwise spec compliant.
    bad=FrameHeader();FrameBlock(bad,plain,true);
    FrameBlock(bad,{3,1,0,0x50,1,2,3,4,5},false);FrameEnd(bad,plain);
    Rejected(bad,"cross-independent-block match","block");
    // Empty blocks do not reset the frame's accumulated content size/hash.
    bad=good;bad[block_data+plain.size()-1]^=1;
    Put32(bad,block_hash,FrameHash({bad.begin()+block_data,bad.begin()+block_hash}));
    Rejected(bad,"valid block checksum cannot hide bad content checksum","content checksum");
    for(const auto& block:std::vector<std::vector<uint8_t>>{
            {0xf0},{0xf0,255},{0x10},{0x10,1,0},{0x10,1,0,0},
            {0x10,1,2,0},{0x1f,1,1,0},{0x1f,1,1,0,255},
            {0x10,1,1,0}, {0x10,1,1,0,0},
            {0x14,1,1,0,0x40,1,2,3,4}, // fewer than five final literals
            {0x10,1,1,0,0x50,1,2,3,4,5}}) { // last match <12 bytes from end
        bad=FrameHeader();FrameBlock(bad,plain,true);FrameBlock(bad,block,false);FrameEnd(bad,plain);
        Rejected(bad,"malformed compressed sequence","block");
    }
    // Small encoded blocks expanding past BD, with a length extension > 64K.
    std::vector<uint8_t> bomb={0x1f,1,1,0};
    bomb.insert(bomb.end(),258,255);bomb.insert(bomb.end(),{0,0x50,1,2,3,4,5});
    bad=FrameHeader(0x60,0x40);FrameBlock(bad,plain,true);FrameBlock(bad,bomb,false);FrameEnd(bad,plain);
    Rejected(bad,"decoded block exceeds BD","block");
}

void TestExplicitSeed() {
    const std::vector<uint8_t> bytes = EncryptedFixture(0x1234ABCDu);
    ::setenv("ARTC_EMOTE_SEED", "0x1234abcd", 1);
    artc::PsbDocument decoded;
    std::string error;
    const bool ok = artc::DecodePsb(bytes, decoded, error);
    ::unsetenv("ARTC_EMOTE_SEED");
    Check(ok, "explicit seed decodes: " + error);
    Check(decoded.root.At("answer").Num(-1) == 42, "explicit seed tree preserved");
}

struct SeedScope {
    uint32_t host=artc::PsbDecryptSeed();
    bool had_env=std::getenv("ARTC_EMOTE_SEED")!=nullptr;
    std::string env=had_env?std::getenv("ARTC_EMOTE_SEED"):"";
    SeedScope(){artc::SetPsbDecryptSeed(0);::unsetenv("ARTC_EMOTE_SEED");}
    ~SeedScope(){
        artc::SetPsbDecryptSeed(host);
        if(had_env)::setenv("ARTC_EMOTE_SEED",env.c_str(),1);else ::unsetenv("ARTC_EMOTE_SEED");
    }
};

void TestSeedPrecedence() {
    SeedScope scope;
    constexpr uint32_t seed=0x1234abcdu;
    const auto bytes=EncryptedFixture(seed);
    artc::PsbDocument doc;std::string error;
    auto decode=[&](const std::string& what) {
        Check(artc::DecodePsb(bytes,doc,error) && doc.root.At("answer").Num(-1)==42,what+": "+error);
    };
    artc::SetPsbDecryptSeed(seed);::setenv("ARTC_EMOTE_SEED","0x11223344",1);
    decode("valid host wins conflicting environment");
    artc::SetPsbDecryptSeed(0x11223344);::setenv("ARTC_EMOTE_SEED","0x1234abcd",1);
    decode("v3 checksum permits environment fallback after wrong host (existing contract)");
    ::setenv("ARTC_EMOTE_SEED","0x55667788",1);
    decode("v3 wrong host and environment fall back to canonical derivation");
    artc::SetPsbDecryptSeed(seed);::setenv("ARTC_EMOTE_SEED","0x11223344",1);
    const auto v2=EncryptedV2Fixture(seed);
    Check(artc::DecodePsb(v2,doc,error) && doc.root.At("text").string=="hello",
          "v2 host wins conflicting environment: "+error);
    artc::SetPsbDecryptSeed(0x11223344);::setenv("ARTC_EMOTE_SEED","0x1234abcd",1);
    Rejected(v2,"v2 cannot authenticate/fallback a wrong host seed","encrypted PSB");

    // A zero anchor is valid for v3 but is not canonical-header derivable;
    // this makes explicit seeds observable rather than hidden by fallback.
    auto no_anchor=emote_fixture::EncodePsb(SampleDocument());
    Put32(no_anchor,8,0);SignV3Header(no_anchor);no_anchor[6]=1;
    Cipher(seed).Apply(no_anchor.data()+8,36);
    artc::SetPsbDecryptSeed(0);::setenv("ARTC_EMOTE_SEED","0x1234abcd",1);
    Check(artc::DecodePsb(no_anchor,doc,error),"valid environment handles noncanonical anchor: "+error);
    for(const char* invalid:{"","0","-1","+305441741"," 305441741","0x1234abcdjunk",
                             "0x11234abcd","18446744073709551616"}) {
        ::setenv("ARTC_EMOTE_SEED",invalid,1);
        Rejected(no_anchor,std::string("invalid environment seed '")+invalid+"'","header");
    }
    ::setenv("ARTC_EMOTE_SEED","0x100000000",1);
    auto wrapped=emote_fixture::EncodePsb(SampleDocument());
    Put32(wrapped,8,0);SignV3Header(wrapped);wrapped[6]=1;
    Cipher(0).Apply(wrapped.data()+8,36);
    Rejected(wrapped,"out-of-range environment must not wrap to seed zero","header");
}

std::vector<uint8_t> EncryptedV4Fixture(uint32_t seed) {
    auto bytes=emote_fixture::EncodePsb(SampleDocument());
    bytes.insert(bytes.begin()+44,12,0);bytes[4]=4;bytes[6]=1;Put32(bytes,8,56);
    for(size_t at:{12u,16u,20u,24u,28u,32u,36u})Put32(bytes,at,Get32(bytes,at)+12);
    // One extended resource, packed 1-byte counts/values. Include a zero-width
    // offset array, which v4 exports may emit for all-zero entries.
    Put32(bytes,44,uint32_t(bytes.size()));bytes.insert(bytes.end(),{13,1,12});
    Put32(bytes,48,uint32_t(bytes.size()));bytes.insert(bytes.end(),{13,1,13,3});
    Put32(bytes,52,uint32_t(bytes.size()));bytes.insert(bytes.end(),{11,22,33});
    std::vector<uint8_t> checksum(bytes.begin()+8,bytes.begin()+40);
    checksum.insert(checksum.end(),bytes.begin()+44,bytes.begin()+56);
    Put32(bytes,40,Adler32(checksum.data(),checksum.size()));
    Cipher(seed).Apply(bytes.data()+8,48);return bytes;
}

void TestEncryptedV4() {
    SeedScope scope;
    const auto bytes=EncryptedV4Fixture(0x42cafe17u);
    artc::PsbDocument doc;std::string error;
    Check(artc::DecodePsb(bytes,doc,error) && doc.version==4 && doc.root.At("answer").Num(-1)==42,
          "encrypted v4 derived header: "+error);
    artc::PsbValue ref;ref.type=artc::PsbValue::Resource;ref.extra=true;
    std::vector<uint8_t> resource;
    Check(doc.ReadResource(ref,resource) && resource==std::vector<uint8_t>({11,22,33}),"v4 extended resource preserved");
    artc::SetPsbDecryptSeed(0x42cafe17u);::setenv("ARTC_EMOTE_SEED","0x11223344",1);
    Check(artc::DecodePsb(bytes,doc,error),"encrypted v4 host/env conflict: "+error);
    auto bad=bytes;bad[44]^=1;Rejected(bad,"v4 extra field checksum corruption","header");
    bad=bytes;bad[40]^=1;Rejected(bad,"v4 checksum corruption","header");
    for(size_t i=0;i<56;++i)Rejected({bytes.begin(),bytes.begin()+i},"v4 truncated header "+std::to_string(i));
    Check(artc::DecodePsb(Lz4Frame(bytes,false,true,28),doc,error),"LZ4 wrapped encrypted v4: "+error);
}

void TestBadPsbStreams() {
    SeedScope scope;
    auto bytes=EncryptedFixture(0x1234abcdu);
    bytes[20]^=1;Rejected(bytes,"v3 corrupt encrypted header","header");
    bytes=EncryptedV2Fixture(0x1234abcdu);
    bytes[12]^=1;Rejected(bytes,"v2 corrupt encrypted name offset","body decrypt/parse failed");
    bytes=EncryptedFixture(0x1234abcdu);bytes.resize(bytes.size()-2);
    Rejected(bytes,"v3 truncated unencrypted body","body parse failed");
    bytes=EncryptedV2Fixture(0x1234abcdu);
    bytes[24]^=0x80;Rejected(bytes,"v2 invalid body range","body range");
    const auto plain=emote_fixture::EncodePsb(SampleDocument());
    for(size_t at:{12u,16u,20u,24u,28u,36u}) {
        bytes=plain;Put32(bytes,at,0xffffffffu);Rejected(bytes,"PSB offset overflow "+std::to_string(at));
    }
    auto with_resource=SampleDocument();with_resource.bytes={1,2,3};with_resource.resources={{0,3}};
    bytes=emote_fixture::EncodePsb(with_resource);Put32(bytes,32,0xffffffffu);
    Rejected(bytes,"PSB used resource data-base overflow","resource");
    // Packed array uint64 counts and elements may not truncate into size_t.
    bytes=plain;const size_t root=Get32(bytes,36);bytes.resize(root);
    bytes.insert(bytes.end(),{20,255,255,255,255,255,255,255,255,12});
    Rejected(bytes,"PSB 64-bit packed-array count overflow","limit");
    bytes=plain;bytes.resize(root);bytes.insert(bytes.end(),{20,0,0,0,0,1,0,0,0,12});
    Rejected(bytes,"PSB 64-bit count must not truncate to zero on 32-bit hosts","limit");
    bytes=plain;bytes.resize(root);bytes.insert(bytes.end(),{32,13,1,20,255,255,255,255,255,255,255,255});
    Rejected(bytes,"PSB 64-bit value offset overflow","offset");
}

std::vector<uint8_t> Mdf(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame={'m','d','f',0};Append32(frame,uint32_t(payload.size()));
    uLongf compressed=compressBound(uLong(payload.size()));frame.resize(8+compressed);
    const int rc=compress2(frame.data()+8,&compressed,payload.data(),uLong(payload.size()),Z_BEST_SPEED);
    Check(rc==Z_OK,"synthetic MDF writer");frame.resize(8+compressed);return frame;
}

void TestMdfAndTransactionalParse() {
    const auto encrypted=EncryptedV2Fixture(0x1234abcdu);
    const auto frame=Mdf(encrypted);
    artc::PsbDocument doc;std::string error;
    Check(artc::DecodePsb(frame,doc,error) && doc.version==2 && doc.root.At("answer").Num(-1)==42,
          "MDF wrapped encrypted v2 unchanged: "+error);
    for(uint32_t size:{0u,uint32_t(encrypted.size()-1),uint32_t(encrypted.size()+1),0xffffffffu}) {
        auto bad=frame;Put32(bad,4,size);Rejected(bad,"MDF size mismatch/overflow","MDF");
    }
    auto bad=frame;bad.pop_back();Rejected(bad,"truncated MDF","MDF");
    bad=frame;bad.push_back(0);Rejected(bad,"MDF trailing data","MDF");
    bad=frame;bad.back()^=1;Rejected(bad,"MDF checksum corruption","MDF");

    // Valid container checksums must never make an invalid decoded PSB valid,
    // nor replace the caller's previous document on a downstream parse error.
    const std::vector<uint8_t> invalid={'n','o','t',' ','P','S','B'};
    Rejected(Lz4Frame(invalid,false,true,28),"valid LZ4 containing invalid PSB","PSB");
    Rejected(Mdf(invalid),"valid MDF containing invalid PSB","PSB");
    // Empty frame and a single empty compressed block are structurally valid,
    // then fail as PSB. This also checks XXH32's empty-input content digest.
    auto empty=FrameHeader(0x7c,0x40,0);FrameEnd(empty,{});
    Rejected(empty,"empty LZ4 content","PSB");
    empty=FrameHeader(0x7c,0x40,0);FrameBlock(empty,{0},false);FrameEnd(empty,{});
    Rejected(empty,"empty compressed LZ4 content","PSB");
}

void TestChecksumRejectsCorruption() {
    std::vector<uint8_t> bytes = EncryptedFixture(0x0BADF00Du);
    bytes[20] ^= 0x01; // corrupt an encrypted header word
    artc::PsbDocument decoded;
    std::string error;
    Check(!artc::DecodePsb(bytes, decoded, error), "corrupt header rejected");
}

void TestPlainStillDecodes() {
    const artc::PsbDocument doc = SampleDocument();
    std::vector<uint8_t> plain = emote_fixture::EncodePsb(doc);
    artc::PsbDocument decoded;
    std::string error;
    Check(artc::DecodePsb(plain, decoded, error), "plain header still decodes");
    Check(decoded.root.At("answer").Num(-1) == 42, "plain tree preserved");
}

} // namespace

int main() {
    SeedScope scope;
    TestEncryptedHeaderRoundTrip();
    TestHostSeedHook();
    TestEncryptedV2Body();
    TestLz4Containers();
    TestFrameHashes();
    TestBlockBoundaries();
    TestBadLz4Frames();
    TestExplicitSeed();
    TestSeedPrecedence();
    TestEncryptedV4();
    TestBadPsbStreams();
    TestMdfAndTransactionalParse();
    TestChecksumRejectsCorruption();
    TestPlainStillDecodes();
    if (g_failures == 0) std::cout << "psb_encrypted_regressions: ok\n";
    return g_failures == 0 ? 0 : 1;
}
