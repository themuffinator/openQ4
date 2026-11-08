#!/usr/bin/env python3
"""Run the production Android interface setup transaction and JNI foreground boundaries.

The JVM half compiles the real InterfaceSettings class with the Activity's
getInterfaceSetup() and the Interface page's saveApplied(). The native half
compiles the real Interface_LoadSetup() JNI reader and feeds it every engine
record the Java side produced, so both ends agree on the wire format.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
JAVA = r'''
package com.darkmatter.openq4;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public class InterfaceSetupTest {
    static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    static class SharedPreferences {
        Map<String,String> memory=new HashMap<>(),disk=new HashMap<>(); boolean fail;
        String getString(String key,String fallback){return memory.getOrDefault(key,fallback);}
        Editor edit(){return new Editor();}
        class Editor {
            Map<String,String> writes=new HashMap<>();
            Editor putString(String key,String value){writes.put(key,value);return this;}
            Editor remove(String key){return putString(key,null);}
            boolean commit(){applyMemory();if(fail)return false;disk=new HashMap<>(memory);return true;}
            void apply(){applyMemory();if(!fail)disk=new HashMap<>(memory);}
            void applyMemory(){writes.forEach((key,value)->{if(value==null)memory.remove(key);else memory.put(key,value);});}
        }
    }
    static class Configuration {int smallestScreenWidthDp=411;float fontScale=1;}
    static class Resources {final Configuration config=new Configuration();Configuration getConfiguration(){return config;}}
    static class Host {
        static final int MODE_PRIVATE=0; final SharedPreferences prefs=new SharedPreferences();final Resources resources=new Resources();
        SharedPreferences getSharedPreferences(String name,int mode){check(name.equals("setup") && mode==0,"wrong preferences");return prefs;}
        Resources getResources(){return resources;}
        @GETTER@
    }
    static class ActivityInfo {static final int SCREEN_ORIENTATION_SENSOR_LANDSCAPE=6,SCREEN_ORIENTATION_FULL_USER=13;}
    @ORIENTATION@
    @SAVE@
    static String expected(int profile,int ui,int text){return profile+":"+ui+":"+text;}
    static String tail(String record){String[] f=record.split(":");return f[2]+":"+f[3]+":"+f[4];}
    public static void main(String[] args) throws IOException {
        int trips=0;
        for(int p=0;p<=5;p++)for(int u=0;u<=200;u+=5)for(int t=0;t<=200;t+=5)for(int o=0;o<=1;o++){
            if((u!=0 && u<75) || (t!=0 && t<100))continue;
            InterfaceSettings a=new InterfaceSettings("0123456789abcdef0123456789abcdef",p,u,t,o),b=InterfaceSettings.decode(a.encode());
            check(b.revision.equals(a.revision) && b.profile==p && b.uiPercent==u && b.textPercent==t && b.orientation==o,"preference round trip");
            check(a.encode().length()<=48 && a.engineValues(false,1).length()<=48 && a.engineValues(true,2).length()<=48,"record exceeds JNI bound");
            // The engine reads sizes only; orientation never changes its record.
            check(a.engineValues(false,1).equals(a.withOrientation(1-o).engineValues(false,1)),"orientation reached the engine record");
            // Records saved before the orientation choice keep their sizes and stay landscape.
            InterfaceSettings legacy=InterfaceSettings.decodeOrNull("1:"+a.revision+":"+p+":"+u+":"+t);
            check(legacy!=null && legacy.profile==p && legacy.uiPercent==u && legacy.textPercent==t
                && legacy.orientation==InterfaceSettings.ORIENTATION_LANDSCAPE,"version 1 record lost or rotated");
            ++trips;
        }
        for(String value:new String[]{null,"","2:0123456789abcdef0123456789abcdef:0:0:0","1:0123456789ABCDEF0123456789abcdef:0:0:0",
                "1:0123456789abcdef0123456789abcde:0:0:0","1:0123456789abcdef0123456789abcdef:6:0:0","1:0123456789abcdef0123456789abcdef:0:70:0",
                "1:0123456789abcdef0123456789abcdef:0:205:0","1:0123456789abcdef0123456789abcdef:0:0:95","1:0123456789abcdef0123456789abcdef:0:101:0",
                "1:0123456789abcdef0123456789abcdef:0:0:0:0","1:0123456789abcdef0123456789abcdef:0:0:0\n","1:0123456789abcdef0123456789abcdef:-1:0:0",
                "2:0123456789abcdef0123456789abcdef:0:0:0:2","2:0123456789abcdef0123456789abcdef:0:0:0:-1","2:0123456789abcdef0123456789abcdef:0:0:0:",
                "2:0123456789abcdef0123456789abcdef:0:0:0:0:0","1:0123456789abcdef0123456789abcdef:0:0:0:1","3:0123456789abcdef0123456789abcdef:0:0:0:1",
                "2:0123456789abcdef0123456789abcdef:0:0:0:1\n"}){
            check(InterfaceSettings.decodeOrNull(value)==null && InterfaceSettings.decode(value)==InterfaceSettings.DEFAULT,"malformed record accepted: "+value);
        }
        for(int[] bad:new int[][]{{-1,0,0,0},{6,0,0,0},{0,74,0,0},{0,205,0,0},{0,77,0,0},{0,0,99,0},{0,0,205,0},{0,0,103,0},{0,0,0,2},{0,0,0,-1}}){
            boolean rejected=false;
            try{new InterfaceSettings("0123456789abcdef0123456789abcdef",bad[0],bad[1],bad[2],bad[3]);}catch(IllegalArgumentException e){rejected=true;}
            check(rejected,"invalid settings constructed: "+Arrays.toString(bad));
        }
        // ui-visual-design.md 13.2: Desk 100/100, Couch 150/125, Handheld 125/100,
        // Phone and Tablet 100% at native density with text from the system font scale.
        InterfaceSettings auto=InterfaceSettings.DEFAULT;
        check(tail(auto.engineValues(false,1)).equals(expected(4,100,100)),"automatic phone profile");
        check(tail(auto.engineValues(true,1)).equals(expected(5,100,100)),"automatic tablet profile");
        check(tail(auto.select(1,0,0).engineValues(false,1.5f)).equals(expected(1,100,100)),"desk ignores phone font scale");
        check(tail(auto.select(2,0,0).engineValues(false,1.5f)).equals(expected(2,150,125)),"couch defaults");
        check(tail(auto.select(3,0,0).engineValues(true,1.5f)).equals(expected(3,125,100)),"handheld defaults");
        float[][] fonts={{.85f,100},{1,100},{1.15f,115},{1.3f,130},{1.5f,150},{2,200},{2.5f,200},{Float.NaN,100},{Float.POSITIVE_INFINITY,100}};
        for(float[] font:fonts){
            check(tail(auto.engineValues(false,font[0])).equals(expected(4,100,(int)font[1])),"phone font scale "+font[0]);
            check(tail(auto.select(5,0,0).engineValues(false,font[0])).equals(expected(5,100,(int)font[1])),"tablet font scale "+font[0]);
        }
        check(tail(auto.select(2,80,190).engineValues(false,2)).equals(expected(2,80,190)),"player sizes did not override profile");
        check(tail(auto.select(4,0,140).engineValues(false,2)).equals(expected(4,100,140)),"explicit text ignored system font");

        // Transaction identity: Apply mints a new revision; editing keeps the draft's.
        InterfaceSettings draft=auto.select(2,0,0),first=draft.applied(),second=draft.applied();
        check(draft.revision.equals(auto.revision) && !first.revision.equals(auto.revision) && !first.revision.equals(second.revision),"apply reused a transaction identity");
        check(first.revision.matches("[0-9a-f]{32}"),"applied identity format");

        // Orientation: landscape unless the player applied Landscape and portrait.
        check(screenOrientation(null)==ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,"unconfigured host left landscape");
        check(screenOrientation(auto)==ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,"default record left landscape");
        check(screenOrientation(auto.withOrientation(1))==ActivityInfo.SCREEN_ORIENTATION_FULL_USER,"portrait choice not honoured");
        InterfaceSettings rotating=auto.select(2,0,0).withOrientation(1);
        check(rotating.revision.equals(auto.revision) && rotating.profile==2,"orientation edit changed the draft identity or sizes");
        check(rotating.applied().orientation==1 && rotating.select(3,0,0).orientation==1,"apply or size edit dropped the orientation");

        Host host=new Host();SharedPreferences prefs=host.prefs;
        check(host.getInterfaceSetup()==null,"unconfigured launcher overwrote engine interface settings");
        prefs.edit().putString(InterfaceSettings.DRAFT,draft.encode()).apply();
        check(host.getInterfaceSetup()==null,"draft reached engine");
        prefs.fail=true;check(!saveApplied(prefs,draft),"failed storage reported success");
        check(host.getInterfaceSetup()==null && prefs.memory.get(InterfaceSettings.DRAFT).equals(draft.encode()),"first failed apply did not restore absence/draft");
        prefs.fail=false;check(saveApplied(prefs,draft),"valid apply rejected");
        String delivered=host.getInterfaceSetup();
        check(delivered!=null && tail(delivered).equals(expected(2,150,125)) && !prefs.memory.containsKey(InterfaceSettings.DRAFT),"applied record not delivered atomically");
        check(prefs.memory.equals(prefs.disk),"saved record did not survive process restart");
        check(delivered.equals(host.getInterfaceSetup()),"unchanged applied record changed identity on a second read");
        InterfaceSettings next=auto.select(4,120,0);prefs.fail=true;
        check(!saveApplied(prefs,next) && delivered.equals(host.getInterfaceSetup()),"failed update changed live engine settings");
        check(prefs.memory.get(InterfaceSettings.DRAFT).equals(next.encode()),"failed update lost draft");
        prefs.fail=false;check(saveApplied(prefs,next),"retry rejected");
        String updated=host.getInterfaceSetup();
        check(!updated.split(":")[1].equals(delivered.split(":")[1]) && tail(updated).equals(expected(4,120,100)),"update kept the old transaction");
        host.resources.config.fontScale=1.3f;
        check(tail(host.getInterfaceSetup()).equals(expected(4,120,130)),"system font scale not read from the live configuration");
        host.resources.config.smallestScreenWidthDp=600;prefs.edit().putString(InterfaceSettings.SAVED,auto.applied().encode()).commit();
        check(tail(host.getInterfaceSetup()).equals(expected(5,100,130)),"tablet width not read from the live configuration");
        prefs.edit().putString(InterfaceSettings.SAVED,"1:corrupt").commit();
        check(host.getInterfaceSetup()==null,"corrupt applied record reached engine");
        prefs.edit().remove(InterfaceSettings.SAVED).commit();
        check(saveApplied(prefs,auto.withOrientation(1)),"orientation apply rejected");
        check(screenOrientation(InterfaceSettings.decodeOrNull(prefs.disk.get(InterfaceSettings.SAVED)))==ActivityInfo.SCREEN_ORIENTATION_FULL_USER,
            "applied orientation did not survive process restart");

        // Every engine record the Activity can produce, for the native reader.
        try(Writer out=new OutputStreamWriter(new FileOutputStream(args[0]),StandardCharsets.US_ASCII)){
            int records=0;
            for(int tablet=0;tablet<2;tablet++)for(float font:new float[]{.85f,1,1.3f,2.5f})
                for(int p=0;p<=5;p++)for(int u:new int[]{0,75,100,150,200})for(int t:new int[]{0,100,125,200}){
                    InterfaceSettings value=new InterfaceSettings("0123456789abcdef0123456789abcdef",p,u,t).applied();
                    out.write(value.engineValues(tablet==1,font)+"\n");++records;
                }
            System.out.println("PASS: "+trips+" preference round trips, malformed recovery, 13.2 profile defaults, system font scale, transaction identity, "
                +"draft isolation, initial/update commit failure, process persistence, version 1 records, orientation policy and actual Activity getter; "+records+" engine records exported");
        }
    }
}
'''
NATIVE = r'''
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include "src/sys/android/android_interface_settings.h"
using jobject=void*;using jclass=void*;using jmethodID=void*;using jstring=void*;
static void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct JNIEnv {
    int failure=0,chars=0,released=0;bool exception=false;std::set<void*> deleted;std::string record;
    jclass GetObjectClass(jobject){if(failure==1){exception=true;return nullptr;}return (void*)2;}
    jmethodID GetMethodID(jclass,const char* name,const char* signature){
        check(!std::strcmp(name,"getInterfaceSetup") && !std::strcmp(signature,"()Ljava/lang/String;"),"wrong Activity getter");
        if(failure==2){exception=true;return nullptr;}return (void*)3;}
    jobject CallObjectMethod(jobject,jmethodID){if(failure==3){exception=true;return nullptr;}return failure==4?nullptr:(void*)4;}
    bool ExceptionCheck(){return exception;}void ExceptionClear(){exception=false;}
    int GetStringUTFLength(jstring){return static_cast<int>(record.size());}
    const char* GetStringUTFChars(jstring,void*){++chars;if(failure==5)return nullptr;return record.c_str();}
    void ReleaseStringUTFChars(jstring,const char* bytes){check(bytes==record.c_str(),"released foreign characters");++released;}
    void DeleteLocalRef(jobject obj){check(deleted.insert(obj).second,"double local-ref release");}
} env;
static bool noEnv=false,noActivity=false;
static void* SDL_GetAndroidJNIEnv(){return noEnv?nullptr:&env;}
static void* SDL_GetAndroidActivity(){return noActivity?nullptr:(void*)1;}
struct Cvars {float scale=1,text=1;int sets=0;
    void SetCVarFloat(const char* name,float value){++sets;
        if(!std::strcmp(name,"ui_retainedScale"))scale=value;else if(!std::strcmp(name,"ui_retainedTextScale"))text=value;else check(false,"unexpected cvar");}
} cvars,*cvarSystem=&cvars;
struct StringCvar {std::string value;int sets=0;const char* GetString() const{return value.c_str();}void SetString(const char* v){value=v;++sets;}} ui_androidInterfaceApplied;
struct Common {int prints=0,warnings=0;void Printf(const char*,...){++prints;}void Warning(const char*){++warnings;}} commonObject,*common=&commonObject;
@FUNCTION@
static const char* kRecord="1:0123456789abcdef0123456789abcdef:2:150:125";
static void reset(const char* record=kRecord){env=JNIEnv{};env.record=record;noEnv=noActivity=false;cvars=Cvars{};ui_androidInterfaceApplied=StringCvar{};commonObject=Common{};}
static void unchanged(){check(!cvars.sets && !ui_androidInterfaceApplied.sets,"invalid setup partially changed cvars");}
static bool near(float a,float b){return a>b-1e-5f && a<b+1e-5f;}
int main(int argc,char** argv){
    reset();Interface_LoadSetup();
    check(near(cvars.scale,1.5f) && near(cvars.text,1.25f) && ui_androidInterfaceApplied.value=="0123456789abcdef0123456789abcdef","valid setup not applied");
    check(env.deleted==std::set<void*>({(void*)1,(void*)2,(void*)4}) && env.chars==1 && env.released==1,"JNI locals or characters leaked");
    check(commonObject.prints==1 && !commonObject.warnings,"applied setup not logged");
    // Returning to the game with the same applied record keeps later in-game changes.
    cvars.scale=1.1f;cvars.sets=0;ui_androidInterfaceApplied.sets=0;env.deleted.clear();
    Interface_LoadSetup();check(near(cvars.scale,1.1f) && !cvars.sets && !ui_androidInterfaceApplied.sets,"unchanged transaction overwrote in-game sizes");
    env.record="1:fedcba9876543210fedcba9876543210:4:100:130";env.deleted.clear();Interface_LoadSetup();
    check(near(cvars.scale,1) && near(cvars.text,1.3f) && ui_androidInterfaceApplied.value=="fedcba9876543210fedcba9876543210","new transaction not applied");
    for(int failure=1;failure<=5;++failure){
        reset();env.failure=failure;Interface_LoadSetup();unchanged();
        check(!env.exception && env.deleted.count((void*)1),"exception or Activity reference leaked");
        check(env.released==env.chars-(failure==5?1:0),"characters not released");
        check(commonObject.warnings==(failure<=3?1:0),"JNI exception not reported");
    }
    reset("1:0123456789abcdef0123456789abcdef:2:150:125:0000");Interface_LoadSetup();unchanged();
    check(!env.chars,"oversized record read past the JNI bound");
    reset(std::string(49,'1').c_str());Interface_LoadSetup();unchanged();check(!env.chars,"49-byte record read");
    for(const char* invalid:{"","1:corrupt","1:0123456789abcdef0123456789abcdef:0:100:100","1:0123456789abcdef0123456789abcdef:4:70:100",
            "1:0123456789abcdef0123456789abcdef:4:100:99","1:0123456789abcdef0123456789abcdef:4:100:100\n"}){
        reset(invalid);Interface_LoadSetup();unchanged();check(env.released==env.chars,"characters leaked on invalid record");
    }
    reset();noEnv=true;Interface_LoadSetup();unchanged();check(env.deleted.empty(),"JNI call without env");
    reset();noActivity=true;Interface_LoadSetup();unchanged();check(env.deleted.empty(),"JNI call without Activity");
    int records=0;
    if(argc>1){
        std::ifstream in(argv[1]);std::string line;
        while(std::getline(in,line)){
            reset(line.c_str());Interface_LoadSetup();
            check(line.size()>35 && line.compare(0,2,"1:")==0,"export format");
            const std::string rest=line.substr(35);const size_t a=rest.find(':'),b=rest.find(':',a+1);
            check(a!=std::string::npos && b!=std::string::npos,"export format");
            const int ui=std::stoi(rest.substr(a+1,b-a-1)),text=std::stoi(rest.substr(b+1));
            check(ui_androidInterfaceApplied.value==line.substr(2,32) && near(cvars.scale,ui/100.0f) && near(cvars.text,text/100.0f),
                ("Java engine record rejected: "+line).c_str());
            ++records;
        }
        check(records>0,"no Java records");
    }
    std::printf("PASS: production JNI interface reader, applied-once transactions, exact payload validation, missing/unconfigured host, exceptions, reference ownership; %d Java records accepted\n",records);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--javac', default=shutil.which('javac') or str(Path(os.environ.get('JAVA_HOME', ''))/'bin/javac.exe'))
    parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++'))
    args = parser.parse_args()
    out = ROOT/'.tmp/android-interface-setup'
    out.mkdir(parents=True, exist_ok=True)
    production = ROOT/'mobile/android/app/src/main/java/com/darkmatter/openq4'
    java_source = JAVA.replace('@GETTER@', function_body((production/'OpenQ4Activity.java').read_text(encoding='utf-8'), 'public String getInterfaceSetup('))
    activity = (production/'OpenQ4Activity.java').read_text(encoding='utf-8')
    java_source = java_source.replace('@ORIENTATION@', function_body(activity, 'static int screenOrientation('))
    # SDL's window-derived request (FULL_USER for openQ4's resizable window)
    # must never replace the player's orientation, which the Activity applies
    # before SDL creates the window and again on each return from setup.
    bis = function_body(activity, 'public void setOrientationBis(')
    assert 'applyScreenOrientation' in bis and 'super.setOrientationBis' not in bis, 'SDL can override the orientation'
    assert 'applyScreenOrientation();' in function_body(activity, 'protected void onCreate('), 'orientation not applied at creation'
    assert 'applyScreenOrientation();' in function_body(activity, 'protected void onResume('), 'orientation not applied on return'
    manifest = (ROOT/'mobile/android/app/src/main/AndroidManifest.xml').read_text(encoding='utf-8')
    assert 'android:screenOrientation="sensorLandscape"' in manifest, 'manifest default orientation changed'
    page = (production/'InterfacePage.java').read_text(encoding='utf-8')
    assert 'R.array.setup_orientations' in page and 'draft.withOrientation(position)' in page, 'interface page lacks orientation'
    java_source = java_source.replace('@SAVE@', function_body((production/'InterfacePage.java').read_text(encoding='utf-8'), 'static boolean saveApplied('))
    launcher = (production/'LauncherActivity.java').read_text(encoding='utf-8')
    assert 'new InterfacePage(this, theme, session.prefs).build(' in launcher, 'setup does not open the interface page'
    assert 'state.putBoolean("interface_page", interfacePage)' in launcher, 'interface page lost on Activity recreation'
    glue = (ROOT/'src/sys/android/android_touch_controls.cpp').read_text(encoding='utf-8')
    frame = function_body(glue, 'void AndroidTouchControls_Frame(')
    entry = frame[:frame.index('touchAppActive = appActive;')]
    assert 'if ( appActive && !touchAppActive )' in entry and 'Interface_LoadSetup();' in entry, 'interface setup is not read on foreground entry only'
    unit = out/'InterfaceSetupTest.java'; unit.write_text(java_source, encoding='utf-8')
    classes = out/'classes'; classes.mkdir(exist_ok=True)
    records = out/'engine-records.txt'
    javac = Path(args.javac).resolve()
    subprocess.run([str(javac), '--release', '17', '-d', str(classes), str(production/'InterfaceSettings.java'), str(unit)], check=True)
    subprocess.run([str(javac.with_name('java.exe' if os.name == 'nt' else 'java')), '-cp', str(classes),
                    'com.darkmatter.openq4.InterfaceSetupTest', str(records)], check=True)
    cpp = out/'jni.cpp'; binary = out/('jni.exe' if os.name == 'nt' else 'jni')
    cpp.write_text(NATIVE.replace('@FUNCTION@', function_body(glue, 'static void Interface_LoadSetup(')), encoding='utf-8')
    env = {**os.environ, 'TMP': str(out), 'TEMP': str(out), 'TMPDIR': str(out)}
    subprocess.run([args.cxx, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), '-o', str(binary)],
                   env=env, cwd=out, check=True)
    subprocess.run([str(binary), str(records)], check=True)


if __name__ == '__main__':
    main()
