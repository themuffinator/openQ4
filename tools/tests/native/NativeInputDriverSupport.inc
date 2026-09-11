// Counted engine edges. The sink/effect bodies inserted below are production.
static std::vector<std::string> effects;
static bool RetainedUI_IsOpen(){return false;}
struct idKeyInput {
    static bool down[K_LAST_KEY];
    static void PreliminaryKeyEvent(int,bool);
    static void PreliminaryMouseEvent(int,int){effects.push_back("pre-mouse");}
    static void PreliminaryJoystickEvent(int){effects.push_back("pre-joystick");}
    static bool IsDown(int key){return down[key];}
    static int GetUsercmdAction(int){return 1;}
    static void ExecKeyBinding(int){effects.push_back("binding");}
};
bool idKeyInput::down[K_LAST_KEY]={};
struct KeySlot{bool& down;};
static constexpr int MAX_KEYS=K_LAST_KEY;
static bool Key_IsControllerKey(int){return false;}
static int key_lastInputFamily=0;static constexpr int KEY_INPUT_FAMILY_CONTROLLER=1,KEY_INPUT_FAMILY_DESKTOP=2;
struct KeySlots{bool operator!=(std::nullptr_t)const{return true;}KeySlot operator[](int key){return {idKeyInput::down[key]};}} keys;
struct Commands {
    unsigned executions=0,buffers=0;
    std::function<void()> callback;
    void ExecuteCommandBuffer(){++executions;}
    void BufferCommandText(int,const char*){++buffers;auto call=std::move(callback);if(call)call();}
} commandObject,*cmdSystem=&commandObject;
static constexpr int CMD_EXEC_APPEND=0;
using idStr=std::string;
static unsigned sessionDeliveries=0,scrollSyncs=0,applicationDispatches=0;
static bool RetainedUI_ProcessEvent(const sysEvent_t*){return false;}
static unsigned retainedFrames=0;
static void RetainedUI_FrameInput(){++retainedFrames;}
class idUserInterface {public:const char* HandleEvent(const sysEvent_t*,int);};
static idUserInterface guiEndpoint;
enum escReply_t {ESC_IGNORE,ESC_GUI,ESC_MAIN};
struct GameEndpoint {
    unsigned calls=0;escReply_t reply=ESC_IGNORE;std::function<void()> callback;
    escReply_t HandleESC(idUserInterface** gui){++calls;*gui=&guiEndpoint;auto call=std::move(callback);if(call)call();return reply;}
};
struct ConsoleEndpoint {
    std::function<void()> callback,closeCallback;bool consumes=false;
    void Close(){auto call=std::move(closeCallback);if(call)call();}
    bool ProcessEvent(const sysEvent_t*,bool){auto call=std::move(callback);if(call)call();return consumes;}
} consoleObject,*console=&consoleObject;
class idSessionLocal {
public:
    idUserInterface* guiActive=&guiEndpoint;idUserInterface* guiTest=nullptr;
    bool mapSpawned=true;GameEndpoint* game=nullptr;std::function<void()> callback;
    unsigned setGuiCalls=0,startMenuCalls=0;
    bool ProcessEvent(const sysEvent_t*);void MenuEvent(const sysEvent_t*);
    bool IsDemoPlaybackActive(){return false;}
    void OpenDemoMenu(bool){}void SetGUI(idUserInterface* gui,void*){++setGuiCalls;guiActive=gui;}
    void StartMenu(){++startMenuCalls;}void TestGUI(const char*){guiTest=nullptr;}
    void DispatchCommand(idUserInterface*,const char*){++applicationDispatches;}
} sessionObject,*session=&sessionObject;
const char* idUserInterface::HandleEvent(const sysEvent_t* event,int) {
    effects.push_back("session-"+std::to_string(event->evValue));++sessionDeliveries;
    auto call=std::move(sessionObject.callback);if(call)call();return "";
}
static bool UI_DispatchApplicationActions(idUserInterface*,const char*,bool&){++applicationDispatches;return true;}
static bool HandleMainMenuSettingsScrollInput(idUserInterface*,int){return false;}
static void SyncMainMenuSettingsScrollPages(idUserInterface*){++scrollSyncs;}
static constexpr int UB_MAX_BUTTONS=128;
struct usercmd_t {int duplicateCount=0;};
class idUsercmdGenLocal {
public:
    void Mouse();void MouseEvent(int,int);void Keyboard();void Key(int,bool);
    void NativeInputChanged() noexcept;
    void NativeInputSource(std::uint64_t,std::uint64_t,std::uint64_t,unsigned,int,bool) noexcept;
    bool NativeKeyBlocked(int)const noexcept;bool Inhibited();
    usercmd_t GetDirectUsercmd();
    void InitCurrent(){++initializations;}
    void Joystick(){++joysticks;}
    void MakeCurrent(){++simulations;}
    void ResetMouseFilter(){++filters;}
    void SetButtonAction(int action,bool down){buttonState[action]+=down?1:-1;}
    unsigned initializations=0,joysticks=0,simulations=0,filters=0;
    bool keyState[K_LAST_KEY]={},directButtonState[UB_MAX_BUTTONS]={},retainedKeyBlocked[K_LAST_KEY]={};
    bool retainedDirectBlocked[UB_MAX_BUTTONS]={},nativeUnknownKeyBlocked[K_LAST_KEY]={};
    struct nativeHeldSource_t {std::uint64_t route=0,window=0,device=0;unsigned source=0;int key=0;bool down=false;} nativeHeldSources[512];
    bool nativeSourceExhausted=false;
    int buttonState[UB_MAX_BUTTONS]={},joystickAxis[8]={};
    int inhibitCommands=0,mouseDx=0,mouseDy=0,continuousMouseX=0,continuousMouseY=0,mouseButton=0;
    bool mouseDown=false;usercmd_t cmd;
} localUsercmdGen;
idEventLoop* eventLoop=nullptr;
