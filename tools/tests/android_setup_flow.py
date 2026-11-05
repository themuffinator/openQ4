#!/usr/bin/env python3
"""Exercise the production setup pipeline, resource formats and public entry point."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from android_document_source import SOURCES as PROVIDER_SOURCES

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
'android/content/Context.java': r'''
package android.content;
import java.io.File;
import android.content.res.AssetManager;
public class Context {
    public Context application=this;
    public File external,files;
    public AssetManager assets;
    public ContentResolver resolver;
    public Context getApplicationContext(){return application;}
    private void check(){if(application!=this)throw new AssertionError("worker retained the Activity context");}
    public File getExternalFilesDir(String type){check();return external;}
    public File getFilesDir(){check();return files;}
    public AssetManager getAssets(){check();return assets;}
    public ContentResolver getContentResolver(){check();return resolver;}
}
''',
'android/content/res/AssetManager.java': r'''
package android.content.res;
import java.io.*;
public final class AssetManager {
    public File root;public int opens;
    public InputStream open(String name)throws IOException {++opens;return new FileInputStream(new File(root,name));}
    public String[] list(String name){return new File(root,name).list();}
}
''',
'android/util/Log.java': r'''
package android.util;
public final class Log {public static int e(String tag,String message,Throwable error){return 0;}}
''',
'com/darkmatter/openq4/SetupFlowTest.java': r'''
package com.darkmatter.openq4;
import static com.darkmatter.openq4.DocumentSourceTest.*;
import android.content.Context;
import android.content.res.AssetManager;
import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.atomic.*;
import java.util.zip.*;

public final class SetupFlowTest {
    static final class Progress implements SetupTask.Reporter {
        final List<SetupTask.Progress> events=new ArrayList<>();
        SetupTask.Phase stopAt;boolean stopped;
        public boolean cancelled(){return stopped;}
        public boolean update(SetupTask.Phase phase,String name,int completed,int files,long bytes,long total) {
            events.add(new SetupTask.Progress(phase,name,completed,files,bytes,total));
            if(phase==stopAt)stopped=true;return !stopped;
        }
    }
    static void available()throws Exception {
        Object lease=StorageSession.INSTANCE.beginSetup();StorageSession.INSTANCE.endSetup(lease);
    }
    static String sha(byte[] data)throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));
    }
    static void write(Path file,byte[] data)throws Exception {Files.createDirectories(file.getParent());Files.write(file,data);}
    public static void main(String[] args)throws Exception {
        Path root=Path.of(args[0]);
        Context app=new Context();app.files=root.resolve("private").toFile();app.external=root.resolve("external").toFile();
        Context activity=new Context();activity.application=app;
        app.assets=new AssetManager();app.assets.root=root.resolve("assets").toFile();
        Path save=app.files.toPath().resolve("saves/baseoq4/keep.save");write(save,"original-save".getBytes());
        StringBuilder manifest=new StringBuilder("openQ4-package-v1\n");
        for(String name:List.of("mod.json","pak0.pk4","pak1.pk4","pak2.pk4")) {
            byte[] data=("bundled-"+name).getBytes();write(app.assets.root.toPath().resolve("baseoq4/"+name),data);
            manifest.append(sha(data)).append('\t').append(data.length).append('\t').append(name).append('\n');
        }
        byte[] manifestBytes=manifest.toString().getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        String version=sha(manifestBytes);
        write(app.assets.root.toPath().resolve("openq4-package-manifest.txt"),manifestBytes);
        write(app.assets.root.toPath().resolve("openq4-package-version.txt"),version.getBytes());
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(ZipOutputStream zip=new ZipOutputStream(bytes)) {zip.putNextEntry(new ZipEntry("fixture.bin"));zip.write(new byte[16384]);zip.closeEntry();}
        byte[] payload=bytes.toByteArray();Path reference=root.resolve("reference.pk4");write(reference,payload);
        int checksum=Pk4Checksum.read(reference.toFile(),true,null).checksum;
        List<RetailData.Expected> catalogue=List.of(new RetailData.Expected("pak001.pk4",checksum,true));
        Progress missing=new Progress();RetailData.Report report=new SetupWork(activity,null,catalogue).run(missing);
        require(!report.ready() && app.assets.opens==0,"missing data prepared engine content or appeared ready");available();
        Provider provider=provider(payload);app.resolver=provider;
        Progress interrupted=new Progress();interrupted.stopAt=SetupTask.Phase.COPYING_PACKAGE;
        cancelled(()->new SetupWork(activity,TREE,catalogue).run(interrupted));available();provider.allClosed();
        Path installed=app.external.toPath().resolve("q4base/pak001.pk4");
        require(Arrays.equals(Files.readAllBytes(installed),payload),"bundled-copy cancellation discarded completed retail import");
        require(!Files.exists(app.files.toPath().resolve("packages/"+version+"/.ready")),"cancelled bundled copy published readiness");
        Provider retry=provider(payload);retry.denyOpen=true;app.resolver=retry;
        Progress progress=new Progress();report=new SetupWork(activity,TREE,catalogue).run(progress);
        require(report.ready() && retry.opened.isEmpty(),"retry failed to reuse verified retail copy");
        require(progress.events.stream().anyMatch(event->event.phase==SetupTask.Phase.CHECKING_EXISTING) &&
                progress.events.stream().anyMatch(event->event.phase==SetupTask.Phase.COPYING_PACKAGE),"pipeline omitted retail/package progress");
        require(Files.readString(app.files.toPath().resolve("packages/"+version+"/.ready")).equals(version),"complete pipeline did not publish package readiness");
        require(report.issues.stream().anyMatch(issue->issue.code==RetailData.Code.MISSING_LANGUAGE && !issue.blocking),"nonblocking language warning lost");
        available();retry.allClosed();
        app.resolver=null;require(new SetupWork(activity,null,catalogue).run(new Progress()).ready(),"installed-data check required provider access");
        available();
        Path preserved=root.resolve("preserved.pk4");Files.move(installed,preserved);
        Provider cancelProvider=provider(payload);app.resolver=cancelProvider;
        SetupWork cancelling=new SetupWork(activity,TREE,catalogue);
        cancelProvider.beforeQuery=()->{try{cancelling.cancel();}catch(Exception error){throw new RuntimeException(error);}};
        cancelled(()->cancelling.run(new Progress()));cancelProvider.allClosed();available();
        require(!Files.exists(installed),"cancelled provider query reached a destination commit");
        Files.move(preserved,installed);
        Context unavailable=new Context();unavailable.external=null;unavailable.files=app.files;
        failed(RetailImport.Code.DESTINATION_UNAVAILABLE,()->new SetupWork(unavailable,null,catalogue).run(new Progress()));available();
        String diagnostics=Files.readString(app.files.toPath().resolve("saves/baseoq4/logs/setup.log"));
        require(diagnostics.contains("Setup cancelled") && diagnostics.contains("Data finding: MISSING_LANGUAGE") &&
                diagnostics.contains("Setup operation failed") && diagnostics.contains("DESTINATION_UNAVAILABLE"),
                "release diagnostics omit setup results or recovery failures");
        Object game=new Object();StorageSession.INSTANCE.activityCreated(game);
        try{new SetupWork(activity,null,catalogue).run(new Progress());throw new AssertionError("setup entered active game storage");}
        catch(StorageSession.Busy expected){}finally{StorageSession.INSTANCE.activityDestroyed(game);}
        available();
        require(Files.readString(save).equals("original-save"),"setup altered private saves");
        System.out.println("PASS: real provider/import/verification/package pipeline; missing-data gate; cancellation between retail and bundled files; "+
            "retry without reopening healthy source; nonblocking language warning; provider cancellation bridge; application-context ownership; "+
            "game/setup exclusion and lease release on success/error/cancellation; exact private-save preservation");
    }
}
'''
}


def resources() -> None:
    base = ROOT/'mobile/android/app/src/main'
    values = {entry.get('name'): ''.join(entry.itertext()) for entry in ET.parse(base/'res/values/strings.xml').getroot()
              if entry.get('translatable') != 'false'}
    formats = lambda value: sorted(re.findall(r'%(\d+)\$[,\d.]*([sdf])', value))
    for locale in ('de', 'es', 'fr'):
        translated = {entry.get('name'): ''.join(entry.itertext()) for entry in ET.parse(base/f'res/values-{locale}/strings.xml').getroot()}
        assert translated.keys() == values.keys(), f'incomplete {locale} resource set'
        for name, text in values.items():
            assert translated[name] and formats(text) == formats(translated[name]), f'{locale}/{name} format mismatch'
    ns = '{http://schemas.android.com/apk/res/android}'
    manifest = ET.parse(base/'AndroidManifest.xml')
    activities = {node.get(ns+'name'): node for node in manifest.iter('activity')}
    assert activities['.OpenQ4Activity'].get(ns+'exported') == 'false', 'native entry bypasses setup'
    launchers = [name for name, node in activities.items() if any(category.get(ns+'name') == 'android.intent.category.LAUNCHER'
                 for category in node.iter('category'))]
    assert launchers == ['.LauncherActivity'], 'unexpected public launcher'
    permissions = [node.get(ns+'name') for node in manifest.iter('uses-permission')]
    assert not any('EXTERNAL_STORAGE' in name for name in permissions), 'folder import requests broad storage access'
    print('PASS: complete setup resource/format sets for English/German/Spanish/French; scoped public launcher')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    java_home = os.environ.get('JAVA_HOME', '')
    parser.add_argument('--javac', default=shutil.which('javac') or (str(Path(java_home)/'bin/javac') if java_home else None))
    args = parser.parse_args()
    if not args.javac:
        parser.error('a JDK is required')
    resources()
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='android-setup-flow-', dir=ROOT/'.tmp') as temporary:
        scratch = Path(temporary)
        paths = []
        for name, text in {**PROVIDER_SOURCES, **SOURCES}.items():
            path = scratch/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8')
            paths.append(str(path))
        production = ROOT/'mobile/android/app/src/main/java/com/darkmatter/openq4'
        paths.extend(str(production/name) for name in ('Pk4Checksum.java', 'RetailData.java', 'RetailImport.java',
                     'DocumentSource.java', 'PackageStorage.java', 'SetupTask.java', 'StorageSession.java', 'SetupWork.java',
                     'SetupDiagnostics.java'))
        classes = scratch/'classes'
        classes.mkdir()
        javac = Path(args.javac).resolve()
        java = javac.with_name('java.exe' if os.name == 'nt' else 'java')
        subprocess.run([str(javac), '--release', '17', '-d', str(classes), *paths], check=True)
        subprocess.run([str(java), '-cp', str(classes), 'com.darkmatter.openq4.SetupFlowTest', str(scratch)], check=True)


if __name__ == '__main__':
    main()
