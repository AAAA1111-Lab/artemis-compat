#include <map>
#include <fstream>
#include <filesystem>
#include <chrono>
#include "core/engine_context.h"
#include "config/ini.h"
#include "pack/pack_manager.h"
#include "render/compositor.h"
#include "script/lua_engine.h"
#include <cstdlib>
#include <iostream>

static int gc_completed=0;
static int RecordGc(lua_State*) { ++gc_completed; return 0; }
static void Check(bool ok, const char *why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
    artc::PackManager packs;
    artc::Compositor compositor;
    compositor.Init(640,480);
    for (const char *id : {"1", "2"}) {
        compositor.SetProps(id, {{"left", id[0]=='1' ? "0" : "160"},
                                {"top","0"},{"w","100"},{"h","100"}});
        for (auto &layer : const_cast<std::vector<artc::Layer>&>(compositor.Layers()))
            if (layer.id == id) layer.texture=1; // synthetic hit geometry, no GL drawing
    }
    artc::LuaEngine script;
    Check(script.Init(&packs,artc::Ini{},"android",640,480,&compositor), "initialize menu callbacks");
    Check(script.DoString(R"(
        opened=0; yes=0; pushes=0; alias=0
        function open_dialog(e,p) opened=opened+1 end
        function go_exit(e,p) yes=yes+1; e:tag{'exit'} end
        function setonpush_call(e,p)
            e:tag{'calllua',['function']=p.lua,btn=p.key}
        end
        function onpush(e,p) pushes=pushes+1 end
        function other_alias(e,p) alias=alias+1 end
        e:tag{'setonpush',key=1,['function']='onpush'}
        e:lyevent{id='1',type='click',click='setonpush_call',lua='open_dialog',key='no'}
    )", "click-only dialog"), "register accepted click alias with self-contained Lua routing");
    script.SetWaiting(true);
    script.ClickAt(50,50); script.RunEnterFrame(); script.EndFrame();
    Check(script.DoString("assert(opened==1 and pushes==0)", "dialog open"),
          "click-only callback opens the dialog exactly once");
    Check(script.IsWaiting() && !script.ConsumeExitRequest(), "opening a dialog preserves the story wait");
    Check(script.DoString(R"(
        btn={name='confirm',cursor='yes',confirm={p={yes={exec='go_exit'}}}}
        function confirm_yes(e,p) assert(p.btn=='yes'); go_exit(e,p) end
        e:lyevent{id='2',type='click',['function']='confirm_yes',key='yes'}
    )", "function-only YES"), "register accepted function alias for a button with exec");
    script.ClickAt(210,50); script.RunEnterFrame(); script.EndFrame();
    Check(script.DoString("assert(yes==1 and pushes==0)", "confirmed exit"),
          "function-only exec callback is not swallowed");
    Check(script.ConsumeExitRequest() && !script.ConsumeExitRequest(), "YES requests host exit exactly once");
    Check(script.IsWaiting(), "event-driven exit does not advance the suspended story");
    Check(script.DoString(R"(
        btn=nil
        e:lyevent{id='1',type='click',['function']='setonpush_call',
                  click='other_alias',lua='open_dialog',key='no'}
    )", "non-exec alias priority"), "register both callback aliases");
    script.ClickAt(50,50); script.RunEnterFrame(); script.EndFrame();
    Check(script.DoString("assert(opened==2 and alias==0 and pushes==0)", "one callback"),
          "non-exec path preserves function priority without duplicate callbacks");
    Check(script.DoString(R"(
        btn={name='confirm',cursor='yes',confirm={p={yes={exec='go_exit'}}}}
        e:lyevent{id='2',type='click',click='confirm_yes',
                  ['function']='other_alias',key='yes'}
    )", "exec alias priority"), "register both callback aliases for exec");
    script.ClickAt(210,50); script.RunEnterFrame(); script.EndFrame();
    Check(script.ConsumeExitRequest() && script.DoString("assert(yes==2 and alias==0)", "exec preferred"),
          "exec path preserves click priority and emits one exit request");
    Check(script.DoString("e:lyevent{id='2',type='click',mode='disable'}", "disabled YES"),
          "disable the button");
    script.ClickAt(210,50); script.RunEnterFrame(); script.EndFrame();
    Check(!script.ConsumeExitRequest() && script.DoString("assert(yes==2)", "disabled confirmation"),
          "disabled button cannot activate the exit callback");
    // Keep the independent retired-runner finalizer regression.
    {
        const std::map<std::string,std::string> files={
            {"system/first.iet", "*top\n[stop]\n"},
            {"story.iet", "*story\n[clickwait]\n[calllua function=story_advanced]\n[stop]\n"},
            {"settings.iet", "*settings\n[calllua function=settings_open]\n[stop]\n"},
            {"nested.iet", "*nested\n[calllua function=nested_open]\n[stop]\n"},
            {"close.iet", "*close\n[return]\n"},
            {"exit.iet", "*yes\n[exit]\n[stop]\n"}
        };
        std::vector<uint8_t> bytes;
        auto put32=[&](uint32_t n){for(int i=0;i<4;++i)bytes.push_back((n>>(i*8))&255);};
        uint32_t index=4;
        for(const auto& f:files)index+=16+f.first.size();
        bytes.insert(bytes.end(),{'p','f','8'});put32(index);put32(files.size());
        uint32_t offset=7+index;
        for(const auto& f:files){
            put32(f.first.size());bytes.insert(bytes.end(),f.first.begin(),f.first.end());
            put32(0);put32(offset);put32(f.second.size());offset+=f.second.size();
        }
        for(const auto& f:files)bytes.insert(bytes.end(),f.second.begin(),f.second.end());
        const auto path=std::filesystem::temp_directory_path()/
            ("artemis-menu-wait-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".pfs");
        {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
        // ResetSession destroys the runner before lua_close runs Lua __gc.
        // The bridge object remains usable, but runner hooks must be detached.
        artc::EngineContext context;
        Check(context.Open(path.string(),"android",{0}) && context.Start(false) && context.BootFramework(),
              "boot synthetic reset/GC session");
        int retired_hook_calls=0;
        context.lua().SetJumpHandler([&](const std::string&,const std::string&){++retired_hook_calls;});
        context.lua().SetCallHandler([&](const std::string&,const std::string&){++retired_hook_calls;});
        context.lua().SetStopHandler([&](const std::string&){++retired_hook_calls;});
        lua_pushcfunction(context.lua().state(),RecordGc);
        lua_setglobal(context.lua().state(),"record_gc");
        Check(context.lua().DoString(R"(
            session_gc=newproxy(true)
            getmetatable(session_gc).__gc=function()
                e:getScriptWaitReason()
                e:getScriptStack()
                e:tag{'jump',file='story.iet',label='story'}
                e:tag{'call',file='settings.iet',label='settings'}
                e:tag{'stop'}
                record_gc()
            end
        )", "session finalizer"), "retain a bridge-querying Lua finalizer until reset");
        context.ResetSession();
        Check(gc_completed==1 && retired_hook_calls==0 && !context.Started(),
              "Lua GC during ResetSession never invokes a destroyed runner's hooks");
        Check(context.Start(false) && context.BootFramework(), "session reboots normally after finalizer teardown");
        context.Shutdown();
        std::filesystem::remove(path);
    }

    std::cout << "menu callback regressions passed\n";
    return 0;
}
