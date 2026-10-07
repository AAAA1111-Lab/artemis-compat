#include "config/ini.h"
#include "core/engine_context.h"
#include "pack/pack_manager.h"
#include "render/compositor.h"
#include "script/lua_engine.h"
#include "script/asb_parser.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
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
    // A complete input -> lyevent -> setonpush -> enqueueTag -> native
    // runner menu round-trip, starting at a real story [clickwait]. No
    // title-specific or commercial framework scripts are assumed here.
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
        artc::PackManager fixtures;
        Check(fixtures.OpenChain(path.string(),{0}), "open synthetic menu/wait pack");
        artc::LuaEngine menu;
        Check(menu.Init(&fixtures,artc::Ini{},"android",640,480,&compositor), "initialize in-game menu");
        artc::AsbRunner runner;
        runner.SetPackSource(&fixtures);runner.SetLuaEngine(&menu);menu.SetScriptRunner(&runner);
        menu.SetJumpHandler([&](const std::string& f,const std::string& l){runner.Jump(f,l);});
        menu.SetCallHandler([&](const std::string& f,const std::string& l){runner.Call(f,l);});
        menu.SetStopHandler([&](const std::string& tag){if(tag=="return")runner.Return();else runner.Halt();});
        auto drain=[&](){
            while(!menu.IsWaiting() && menu.HasQueuedTag()){
                std::string name,file,label;
                std::vector<std::pair<std::string,std::string>> attrs;
                if(!menu.PopQueuedTag(&name,&attrs))break;
                for(const auto& kv:attrs){if(kv.first=="file")file=kv.second;else if(kv.first=="label")label=kv.second;}
                if(name=="call")runner.Call(file,label);
                else if(name=="jump")runner.Jump(file,label);
                else menu.DispatchTag(name,attrs,false);
            }
        };
        auto boundary=[&](){
            drain();
            for(int i=0;i<30 && !menu.IsWaiting() && !runner.Halted();++i){runner.ExecuteLine(menu);drain();}
        };
        auto click=[&](float x){menu.ClickAt(x,50);menu.RunEnterFrame();boundary();menu.EndFrame();};
        Check(menu.DoString(R"(
            opened=0; nested=0; chosen=0; advanced=0; routed=0; route='open_settings'
            function story_advanced(e,p) advanced=advanced+1 end
            function settings_open(e,p) opened=opened+1 end
            function nested_open(e,p) nested=nested+1 end
            function cursor_sync(e,p) end
            function dispatch_key(e,p) routed=routed+1; _G[route](e,p) end
            function open_settings(e,p) e:enqueueTag{'call',file='settings.iet',label='settings'} end
            function open_nested(e,p) e:enqueueTag{'call',file='nested.iet',label='nested'} end
            function close_menu(e,p) e:enqueueTag{'jump',file='close.iet',label='close'} end
            function choose(e,p) chosen=chosen+1 end
            function choose_option(e,p) e:enqueueTag{'calllua',['function']='choose'} end
            function delayed_option(e,p)
                e:enqueueTag{'wait',time=10000}
                e:enqueueTag{'calllua',['function']='choose'}
            end
            function jump_from_queued_lua(e,p)
                e:getScriptWaitReason() -- a query must not end the in-flight event
                e:tag{'jump',file='settings.iet',label='settings'}
            end
            function open_through_lua(e,p) e:enqueueTag{'calllua',['function']='jump_from_queued_lua'} end
            function confirm_exit(e,p) e:enqueueTag{'call',file='exit.iet',label='yes'} end
            function option_router(e,p) e:tag{'calllua',['function']=p.lua} end
            e:tag{'setonpush',key=1,['function']='dispatch_key'}
            e:lyevent{id='1',type='click',['function']='cursor_sync'}
            e:lyevent{id='2',type='click',['function']='option_router',lua='choose_option'}
        )", "queued menu callbacks"), "register actual lyevent/setonpush queue routing");
        Check(runner.Jump("story.iet","story"), "start story click-wait fixture");boundary();
        Check(menu.IsWaiting(), "story stops at its clickwait");
        const size_t story_pc=runner.CurrentIndex();
        click(50);
        Check(runner.CurrentFile()=="settings.iet" && runner.Halted() && !menu.IsWaiting() &&
              runner.StackFiles().size()==2 && menu.DoString("assert(opened==1 and advanced==0 and routed==1)","settings entry"),
              "in-game settings enters at the first click without advancing the story");
        click(210);
        Check(menu.DoString("assert(chosen==1 and routed==1)","option activates"),
              "settings option callback is processed once on the same frame");
        Check(menu.DoString("route='open_nested'", "nested route"), "select nested menu route");click(50);
        Check(runner.CurrentFile()=="nested.iet" && runner.StackFiles().size()==3 &&
              menu.DoString("assert(nested==1)", "nested entry"), "nested queued settings call adds one frame");
        Check(menu.DoString("route='close_menu'", "close nested"), "select nested close route");click(50);
        Check(runner.CurrentFile()=="settings.iet" && runner.Halted() && !menu.IsWaiting() &&
              runner.StackFiles().size()==2, "queued jump close returns to the real settings caller");
        Check(menu.DoString(R"(
            e:lyevent{id='2',type='click',['function']='option_router',lua='delayed_option'}
        )", "delayed option"), "register option with its own wait");click(210);
        Check(menu.IsWaiting() && menu.QueuedTagName()=="calllua" &&
              menu.DoString("assert(chosen==1)","option still waiting"), "option eqwait stops before its queued callback");
        // A second event while the queued wait is active shares that pending
        // event, rather than overwriting the scenario's saved wait.
        Check(menu.DoString(R"(
            e:lyevent{id='2',type='click',['function']='option_router',lua='choose_option'}
        )", "second pending click"), "register another option while waiting");click(210);
        Check(menu.IsWaiting() && menu.DoString("assert(chosen==1)","second click blocked"),
              "pending event cannot run the second click through a queued wait");
        menu.SetWaiting(false);boundary();
        Check(!menu.IsWaiting() && !menu.HasQueuedTag() && menu.DoString("assert(chosen==3)","delayed callbacks finish"),
              "both accepted option callbacks run once after the queued wait finishes");
        click(50);
        Check(runner.CurrentFile()=="story.iet" && runner.CurrentIndex()==story_pc && menu.IsWaiting() &&
              runner.StackFiles().size()==1 && menu.DoString("assert(advanced==0)","story not advanced"),
              "settings close restores the original story cursor and clickwait");
        Check(menu.DoString("route='open_through_lua'", "queued Lua menu route"), "select indirect menu route");click(50);
        Check(!menu.IsWaiting() && runner.CurrentFile()=="settings.iet" && runner.StackFiles().size()==2,
              "wait query within the final queued Lua callback does not prematurely close its event");
        Check(menu.DoString("route='close_menu'", "close indirect menu"), "select indirect close route");click(50);
        Check(menu.IsWaiting() && runner.CurrentFile()=="story.iet" && runner.CurrentIndex()==story_pc &&
              runner.StackFiles().size()==1, "indirect queued Lua jump returns to the suspended story");
        Check(menu.DoString("route='confirm_exit'", "exit route"), "select exit confirmation");click(50);
        Check(menu.ConsumeExitRequest() && !menu.ConsumeExitRequest() &&
              runner.CurrentFile()=="exit.iet" && menu.DoString("assert(advanced==0)","exit keeps story"),
              "queued in-game exit requests host termination once without consuming story input");
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
