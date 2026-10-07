#include "config/ini.h"
#include "pack/pack_manager.h"
#include "render/compositor.h"
#include "script/lua_engine.h"
#include <cstdlib>
#include <iostream>

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
    std::cout << "menu callback regressions passed\n";
    return 0;
}
