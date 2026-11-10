#!/usr/bin/env python3
"""Exercise setup-worker ownership, Activity rebinding and cancellation delivery."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
JAVA = r'''
package com.darkmatter.openq4;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.util.function.*;

public final class SetupTaskTest {
    static void require(boolean value,String reason){if(!value)throw new AssertionError(reason);}
    static class ActivityNotFoundException extends RuntimeException {}
    static class Intent {
        final Class<?> target;final Map<String,String> extras=new HashMap<>();
        Intent(Object owner,Class<?> target){this.target=target;}
        Intent putExtra(String key,String value){extras.put(key,value);return this;}
        String getStringExtra(String key){return extras.get(key);}
    }
    static class ApplicationInfo {static final int FLAG_DEBUGGABLE=2;}
    static final String EXTRA_DIAGNOSTIC_LAUNCH="diagnostic_launch",DIAGNOSTIC_HOLD="hold";
    // PRODUCTION_DIAGNOSTIC_LAUNCH
    static void diagnosticLaunchGate() {
        // Debuggable builds only, and only the two modes: a release build's exported
        // setup entry point never starts the game without the player's choice.
        for(String mode:new String[]{"sp","mp","SP","map","",null}){
            Intent intent=new Intent(null,LauncherActivity.class);if(mode!=null)intent.putExtra(EXTRA_DIAGNOSTIC_LAUNCH,mode);
            Boolean debug=diagnosticLaunch(intent,ApplicationInfo.FLAG_DEBUGGABLE|1),release=diagnosticLaunch(intent,1);
            require(release==null,"a release build honoured a diagnostic launch: "+mode);
            require("sp".equals(mode)?Boolean.FALSE.equals(debug):"mp".equals(mode)?Boolean.TRUE.equals(debug):debug==null,
                    "diagnostic launch mode mismatch: "+mode);
        }
        require(diagnosticLaunch(null,ApplicationInfo.FLAG_DEBUGGABLE)==null,"a missing intent launched");
        Intent hold=new Intent(null,LauncherActivity.class).putExtra(EXTRA_DIAGNOSTIC_LAUNCH,"hold");
        require(diagnosticHold(hold,ApplicationInfo.FLAG_DEBUGGABLE) && !diagnosticHold(hold,1) && diagnosticLaunch(hold,ApplicationInfo.FLAG_DEBUGGABLE)==null,
                "hold must keep the screen on in debuggable builds only, and never launch");
    }
    static class LauncherActivity {static final String EXTRA_PAGE="setup_page",PAGE_TOUCH_LAYOUT="touch_layout";}
    static class R {static class string {static final int setup_open_failed=1;}}
    static class Log {static void i(String tag,String text){}static void e(String tag,String text,Throwable error){}}
    static class Toast {
        static final int LENGTH_LONG=1;static int shown;
        static Toast makeText(Object context,int text,int duration){require(text==R.string.setup_open_failed,"wrong setup failure");return new Toast();}
        void show(){++shown;}
    }
    static class SDLActivity {
        static final int COMMAND_USER=0x8000;
        boolean mBrokenLibraries,mActivityCreated=true,finishing,fail;
        int starts,unknown,resumes,finishes;Intent last;
        protected boolean onUnhandledMessage(int command,Object value){++unknown;return false;}
        protected void onResume(){++resumes;}
        boolean isFinishing(){return finishing;}
        void startActivity(Intent intent){require(intent.target==LauncherActivity.class,"wrong target");if(fail)throw new ActivityNotFoundException();++starts;last=intent;}
        void finish(){++finishes;}
    }
    static class GameActivity extends SDLActivity {
        static final int SETUP_PAGE_HOME=0,SETUP_PAGE_TOUCH_LAYOUT=1;
        private boolean setupOpening;
        int orientations;
        // Production onResume() reapplies the saved orientation (AND-04); the
        // orientation policy itself is covered by android_interface_setup.py.
        private void applyScreenOrientation(){++orientations;}
        // PRODUCTION_SETUP_REENTRY
    }
    static void reentry() throws Exception {
        GameActivity game=new GameActivity();StorageSession storage=StorageSession.INSTANCE;
        storage.activityCreated(game);require(storage.beginEngine(game),"live engine did not acquire");
        require(!game.onUnhandledMessage(0x8001,null) && game.unknown==1 && game.starts==0,"foreign SDL message intercepted");
        game.onUnhandledMessage(0x8000,0);game.onUnhandledMessage(0x8000,0);
        require(game.starts==1 && game.finishes==0 && storage.activityActive(),"duplicate setup launch or game torn down");
        try{storage.beginSetup();throw new AssertionError("setup entered live game files");}catch(StorageSession.Busy expected){}
        game.onResume();game.onUnhandledMessage(0x8000,0);
        require(game.resumes==1 && game.orientations==1 && game.starts==2,"SDL resume, orientation reapply or second handoff lost");
        game.onResume();game.fail=true;game.onUnhandledMessage(0x8000,0);game.fail=false;game.onUnhandledMessage(0x8000,0);
        require(Toast.shown==1 && game.starts==3,"failed handoff was not reported/retryable");
        require(game.last.extras.isEmpty(),"the setup home page asked for a sub-page");
        // Settings > Controls > Touch layout opens the layout editor itself.
        game.onResume();game.onUnhandledMessage(0x8000,1);
        require(game.starts==4 && "touch_layout".equals(game.last.extras.get(LauncherActivity.EXTRA_PAGE)),"layout editor request lost its page");
        game.onResume();game.onUnhandledMessage(0x8000,7);
        require(game.starts==5 && game.last.extras.isEmpty(),"unknown page argument opened a sub-page");
        for(int state=0;state<3;++state){
            GameActivity inactive=new GameActivity();inactive.finishing=state==0;inactive.mBrokenLibraries=state==1;inactive.mActivityCreated=state!=2;
            require(inactive.onUnhandledMessage(0x8000,0) && inactive.starts==0,"inactive game launched setup");
        }
        storage.activityDestroyed(game);
        try{storage.beginSetup();throw new AssertionError("setup entered closing engine files");}catch(StorageSession.Busy expected){}
        storage.endEngine(game);Object lease=storage.beginSetup();storage.endSetup(lease);
    }
    static void waitFor(BooleanSupplier condition)throws Exception {
        long end=System.nanoTime()+TimeUnit.SECONDS.toNanos(10);
        while(!condition.getAsBoolean() && System.nanoTime()<end)Thread.sleep(1);
        require(condition.getAsBoolean(),"fixture timed out");
    }
    static final class Queue implements SetupTask.Dispatcher {
        final ConcurrentLinkedQueue<Runnable> pending=new ConcurrentLinkedQueue<>();
        final List<Runnable> all=Collections.synchronizedList(new ArrayList<>());
        public void post(Runnable callback){all.add(callback);pending.add(callback);}
        void drain(){Runnable next;while((next=pending.poll())!=null)next.run();}
        void complete(SetupTask<?> task)throws Exception {
            waitFor(()->{drain();return !task.state().busy();});
        }
    }
    public static void main(String[] args)throws Exception {
        reentry();
        diagnosticLaunchGate();
        Thread main=Thread.currentThread();Queue queue=new Queue();SetupTask<String> task=new SetupTask<>(queue);
        List<SetupTask.State<String>> oldViews=new ArrayList<>(),newViews=new ArrayList<>();
        SetupTask.Observer<String> old=state->{require(Thread.currentThread()==main,"worker called UI observer");oldViews.add(state);};
        SetupTask.Observer<String> next=state->{require(Thread.currentThread()==main,"worker called UI observer");newViews.add(state);};
        task.bind(old);require(task.state().status==SetupTask.Status.IDLE && oldViews.size()==1,"initial delivery");
        CountDownLatch entered=new CountDownLatch(1),release=new CountDownLatch(1);
        AtomicBoolean workerRejected=new AtomicBoolean();
        require(task.start(reporter->{
            try{task.state();}catch(IllegalStateException expected){workerRejected.set(true);}
            entered.countDown();release.await();return "first";
        }),"first operation refused");
        require(entered.await(10,TimeUnit.SECONDS),"worker did not start");
        require(!task.start(reporter->"duplicate"),"parallel setup writer admitted");
        task.bind(next);task.unbind(old);int oldCount=oldViews.size();
        release.countDown();queue.complete(task);
        require(workerRejected.get() && task.state().status==SetupTask.Status.SUCCEEDED && task.state().result.equals("first"),
            "owner thread/result handoff");
        require(oldViews.size()==oldCount && newViews.get(newViews.size()-1).result.equals("first"),
            "old Activity detached the new Activity or received stale UI updates");
        task.unbind(next);int newCount=newViews.size();
        require(task.start(reporter->"background"),"background operation refused");queue.complete(task);
        require(newViews.size()==newCount,"detached Activity still received callbacks");task.bind(next);
        require(newViews.get(newViews.size()-1).result.equals("background"),"recreated Activity lost completed state");

        Queue burstQueue=new Queue();SetupTask<String> burst=new SetupTask<>(burstQueue);
        require(burst.start(reporter->{
            for(int i=0;i<=4000;++i)require(reporter.update(SetupTask.Phase.COPYING_DATA,"pak001.pk4",0,1,i,4000),"unexpected cancellation");
            return "copied";
        }),"burst operation refused");
        waitFor(()->burstQueue.pending.size()==2);
        require(burstQueue.all.size()==2,"progress created an unbounded Android message backlog");
        Runnable oldProgress=burstQueue.all.get(0),oldCompletion=burstQueue.all.get(1);
        burstQueue.complete(burst);
        require(burst.state().progress.bytes==4000 && burst.state().result.equals("copied"),"coalescing lost latest progress");
        CountDownLatch secondEntered=new CountDownLatch(1),secondRelease=new CountDownLatch(1);
        burst.start(reporter->{secondEntered.countDown();secondRelease.await();return "second";});
        require(secondEntered.await(10,TimeUnit.SECONDS),"second worker did not start");
        oldProgress.run();oldCompletion.run();
        require(burst.state().status==SetupTask.Status.RUNNING && burst.state().progress==null && burst.state().result==null,
            "old operation altered current state");
        secondRelease.countDown();burstQueue.complete(burst);

        CountDownLatch active=new CountDownLatch(1),interrupted=new CountDownLatch(1),cleanup=new CountDownLatch(1),providerCancel=new CountDownLatch(1);
        AtomicBoolean closedOffUi=new AtomicBoolean(),cleaned=new AtomicBoolean();
        task.start(new SetupTask.Work<String>() {
            public String run(SetupTask.Reporter reporter)throws Exception {
                active.countDown();
                try {new CountDownLatch(1).await();}
                catch(InterruptedException expected){interrupted.countDown();cleanup.await();}
                finally {cleaned.set(true);}
                return "must not publish success";
            }
            public void cancel(){closedOffUi.set(Thread.currentThread()!=main);providerCancel.countDown();}
        });
        require(active.await(10,TimeUnit.SECONDS),"cancellable worker did not start");task.cancel();task.cancel();
        require(task.state().status==SetupTask.Status.STOPPING && !task.start(reporter->"too early"),
            "cancel admitted a new writer before cleanup");
        require(interrupted.await(10,TimeUnit.SECONDS) && providerCancel.await(10,TimeUnit.SECONDS) && closedOffUi.get(),
            "cancel did not interrupt worker and close provider off UI");
        require(!cleaned.get(),"fixture cleanup did not retain ownership");cleanup.countDown();queue.complete(task);
        require(cleaned.get() && task.state().status==SetupTask.Status.CANCELLED && task.state().result==null && task.state().failure==null,
            "cancelled work was launched as success or failed before cleanup");
        task.start(reporter->"retry");queue.complete(task);require(task.state().result.equals("retry"),"cancelled task could not retry");

        Queue lateQueue=new Queue();SetupTask<String> late=new SetupTask<>(lateQueue);
        late.start(reporter->"ready but not delivered");waitFor(()->lateQueue.pending.size()==1);
        late.cancel();lateQueue.complete(late);
        require(late.state().status==SetupTask.Status.CANCELLED && late.state().result==null,"late cancel auto-launched queued success");
        task.start(reporter->{Thread.currentThread().interrupt();return "interrupted normal return";});queue.complete(task);
        require(task.state().status==SetupTask.Status.CANCELLED,"worker interruption was published as success");

        IOException fault=new IOException("injected provider error");task.start(reporter->{throw fault;});queue.complete(task);
        require(task.state().status==SetupTask.Status.FAILED && task.state().failure==fault && task.state().result==null,
            "structured failure or original cause lost");
        task.start(reporter->{reporter.update(SetupTask.Phase.COPYING_DATA,"pak001.pk4",0,1,2,1);return "bad progress";});queue.complete(task);
        require(task.state().status==SetupTask.Status.FAILED && task.state().failure instanceof IllegalArgumentException,
            "invalid progress reached UI");
        task.start(reporter->"recovered");queue.complete(task);
        require(task.state().status==SetupTask.Status.SUCCEEDED && task.state().failure==null && task.state().result.equals("recovered"),
            "error state leaked into retry");
        task.cancel();require(task.state().status==SetupTask.Status.SUCCEEDED,"idle cancellation rewrote completed result");
        StorageSession storage=new StorageSession();Object setup=storage.beginSetup(),activity=new Object();
        try{storage.beginSetup();throw new AssertionError("parallel storage owner admitted");}catch(StorageSession.Busy expected){}
        storage.activityCreated(activity);
        AtomicBoolean nativeEntered=new AtomicBoolean();AtomicReference<Throwable> nativeFailure=new AtomicReference<>();
        Thread nativeThread=new Thread(()->{try{nativeEntered.set(storage.beginEngine(activity));}catch(Throwable error){nativeFailure.set(error);}});
        nativeThread.start();waitFor(()->nativeThread.getState()==Thread.State.WAITING);
        require(!nativeEntered.get(),"native startup overlapped active setup");
        storage.endSetup(setup);nativeThread.join(5000);
        require(nativeEntered.get() && nativeFailure.get()==null && !nativeThread.isAlive(),"engine did not acquire after setup cleanup");
        storage.activityDestroyed(activity);
        require(storage.gameActive() && !storage.activityActive(),"Activity destruction released a still-running native engine");
        try{storage.beginSetup();throw new AssertionError("setup overlapped SDL's timed-out join");}catch(StorageSession.Busy expected){}
        storage.endEngine(activity);require(!storage.gameActive(),"native completion retained dead game owner");
        setup=storage.beginSetup();Object recreated=new Object();storage.activityCreated(recreated);
        AtomicBoolean abandoned=new AtomicBoolean(true);
        Thread abandonedThread=new Thread(()->{try{abandoned.set(storage.beginEngine(recreated));}catch(Throwable error){nativeFailure.set(error);}});
        abandonedThread.start();waitFor(()->abandonedThread.getState()==Thread.State.WAITING);
        storage.activityDestroyed(recreated);abandonedThread.join(5000);
        require(!abandonedThread.isAlive() && !abandoned.get() && nativeFailure.get()==null,"destroyed waiting Activity started native code");
        storage.endSetup(setup);
        Object activeActivity=new Object();storage.activityCreated(activeActivity);
        try{storage.beginSetup();throw new AssertionError("setup overlapped pending game startup");}catch(StorageSession.Busy expected){}
        storage.activityDestroyed(new Object());require(storage.gameActive(),"stale Activity released current owner");
        storage.activityDestroyed(activeActivity);
        require(SetupLanguage.engine("fr").equals("french") && SetupLanguage.engine(null)==null &&
                SetupLanguage.engine("+quit")==null && SetupLanguage.index("unknown")==0,"language boundary");
        System.out.println("PASS: UI-thread ownership; Activity rebind/unbind; retained background completion; bounded latest progress; "+
            "stale callback rejection; one writer through cancellation/cleanup; provider close off UI; late/interrupted success rejection; failure/retry; "+
            "setup/native exclusion through Activity destruction and delayed SDL shutdown; cancelled startup; language allowlist; "+
            "production setup handoff, duplicate suppression, return/reopen, failure/retry and inactive-host guards");
    }
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    java_home = os.environ.get('JAVA_HOME', '')
    parser.add_argument('--javac', default=shutil.which('javac') or
                        (str(Path(java_home)/'bin/javac') if java_home else None))
    args = parser.parse_args()
    if not args.javac:
        parser.error('a JDK is required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='android-setup-task-', dir=ROOT/'.tmp') as temporary:
        scratch = Path(temporary)
        source = scratch/'SetupTaskTest.java'
        activity = (ROOT/'mobile/android/app/src/main/java/com/darkmatter/openq4/OpenQ4Activity.java').read_text()
        methods = '\n'.join(function_body(activity, signature) for signature in (
            'protected boolean onUnhandledMessage(', 'protected void onResume('))
        launcher = (ROOT/'mobile/android/app/src/main/java/com/darkmatter/openq4/LauncherActivity.java').read_text(encoding='utf-8')
        gate = function_body(launcher, 'static Boolean diagnosticLaunch(') + '\n' + function_body(launcher, 'static boolean diagnosticHold(')
        changed = function_body(launcher, 'public void changed(')
        # The launch waits for setup's own successful data check, never bypasses it.
        assert 'diagnosticLaunch != null && ready && !game && available()' in changed, 'diagnostic launch skips the data check'
        assert 'state.status != SetupTask.Status.SUCCEEDED' in changed and 'run(false)' in changed, 'diagnostic launch never checks data'
        # Long setup work and a pending diagnostic launch keep the screen on, so
        # they never stall behind the lock screen.
        assert 'if (busy || diagnosticLaunch != null || diagnosticHold) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);' in changed
        assert 'else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);' in changed
        source.write_text(JAVA.replace('// PRODUCTION_SETUP_REENTRY', methods).replace('// PRODUCTION_DIAGNOSTIC_LAUNCH', gate))
        classes = scratch/'classes'
        classes.mkdir()
        javac = Path(args.javac).resolve()
        java = javac.with_name('java.exe' if os.name == 'nt' else 'java')
        production = ROOT/'mobile/android/app/src/main/java/com/darkmatter/openq4'
        subprocess.run([str(javac), '--release', '17', '-d', str(classes), str(source),
                        *[str(production/name) for name in ('SetupTask.java','StorageSession.java','SetupLanguage.java')]], check=True)
        subprocess.run([str(java), '-cp', str(classes), 'com.darkmatter.openq4.SetupTaskTest'], check=True)


if __name__ == '__main__':
    main()
