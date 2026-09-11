#include "src/framework/NativeInputDispatch.h"
#include <cstdio>
#include <cstdlib>
struct sysEvent_s{int value=42;};class idEventLoop{};
#define CHECK(x) do{if(!(x))std::abort();}while(false)
int main(){sysEvent_t event;int a=12,b=34;bool down=true;idEventLoop loop;
 CHECK(NativeInput_TakeSession(event)==nativeInputSessionResult_t::Legacy);
 CHECK(NativeInput_TakeDeferredSession(event)==nativeInputSessionResult_t::Stop&&event.value==42);
 CHECK(!NativeInput_CompleteSession()&&!NativeInput_BeginMouse()&&!NativeInput_NextMouse(a,b)&&a==12&&b==34);
 CHECK(!NativeInput_CompleteMouse()&&!NativeInput_BeginKeyboard()&&!NativeInput_NextKeyboard(a,down)&&a==12&&down&&!NativeInput_CompleteKeyboard());
 CHECK(NativeInput_SessionCurrent()&&!NativeInput_OwnsPump()&&!NativeInput_CanPrepareCollection()&&NativeInput_FatalRetire()&&!NativeInput_Inhibited()&&!NativeInput_TypedPollDelivery());
 NativeInput_AbortDelivery();NativeInput_BeginLegacySession();NativeInput_EndLegacySession();NativeInput_EndMouse();NativeInput_EndKeyboard();NativeInput_ContinueDeferred(loop);NativeInput_BeginFrame(123);
 CHECK(event.value==42&&!NativeInput_OwnsPump());std::puts("C++17 disabled facade: PASS");
}
