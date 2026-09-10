#!/usr/bin/env python3
"""Compile the complete production EventLoop.cpp against counted boundaries.

The public event enum/struct and EventLoop.h remain actual source inputs. No
engine, game, input injection or live journal is used. Mutations are written
only into temporary alternate include roots; checkout newlines are normalized
for those copies while all provenance hashes bind original bytes.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
SOURCES=["src/framework/EventLoop.cpp","src/framework/EventLoop.h","src/sys/sys_public.h",
         "tools/tests/native/EventJournalTest.cpp","tools/tests/event_journal.py","src/sys/KeyEventMetadata.h",
         "src/sys/EventQueueContinuity.h","src/sys/EventQueueContinuity.cpp","src/sys/EventDisposition.h","src/sys/EventDisposition.cpp"]


def sha(path:Path)->str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main()->int:
    compiler=next((found for name in ("clang++","g++","c++") if (found:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError("A C++17 compiler is required")
    temporary=ROOT/".tmp"
    temporary.mkdir(exist_ok=True)
    scratch=Path(tempfile.mkdtemp(prefix="event-journal-",dir=temporary))
    env=dict(os.environ,TEMP=str(scratch),TMP=str(scratch),TMPDIR=str(scratch))
    before={name:sha(ROOT/name) for name in SOURCES}
    contract=(ROOT/SOURCES[2]).read_text(encoding="utf-8")
    enum=re.search(r"typedef enum\s*\{\s*SE_NONE\b[\s\S]*?\}\s*sysEventType_t;",contract)
    record=re.search(r"typedef struct sysEvent_s\s*\{[\s\S]*?\}\s*sysEvent_t;",contract)
    if not enum or not record:
        raise RuntimeError("Cannot find the production event ABI declarations")
    generated=scratch/"JournalEventContract.h"
    generated.write_text("#pragma once\n"+enum.group()+"\n"+record.group()+"\n",encoding="utf-8",newline="\n")
    production=(ROOT/SOURCES[0]).read_text(encoding="utf-8")
    mutations=[
        ("pushed-continuity-lost","\t\tSys_InvalidateEventQueue();", "\t\t// Lost pushed-queue invalidation.",3),
        ("lifecycle-continuity-lost","\tSys_InvalidateEventQueue();", "\t// Lost event-loop lifecycle invalidation.",5),
        ("key-metadata-unchecked","if ( ev.evType == SE_KEY && ev.evPtrLength )", "if ( false && ev.evType == SE_KEY && ev.evPtrLength )",1),
        ("unbounded-length","if ( length < 0 || length > MAX_JOURNAL_EVENT_PAYLOAD )","if ( false )",1),
        ("unknown-type",'return "Invalid journal event type";',"return NULL;",1),
        ("recorded-address", "candidate.evPtrLength = length;", "candidate.evPtrLength = length;\n\tmemcpy( &candidate.evPtr, header + offsetof( sysEvent_t, evPtr ), sizeof( candidate.evPtr ) );",1),
        ("unterminated-console","if ( ev.evType == SE_CONSOLE && memchr(","if ( false && ev.evType == SE_CONSOLE && memchr(",1),
        ("address-disclosure","journalEvent.evPtr = NULL;","journalEvent.evPtr = ev.evPtr;",1),
        ("private-disclosure","if ( EventLoop_IsPrivateConsoleEvent( ev ) ) {","if ( false && EventLoop_IsPrivateConsoleEvent( ev ) ) {",1),
        ("exception-leak","~idScopedEventPayload() { Reset(); }","~idScopedEventPayload() {}",1),
        ("failure-publication","idScopedEventPayload payload( candidate, true );","out = candidate;\n\tidScopedEventPayload payload( candidate, true );",1),
        ("fatal-before-release","\t\t\tpayload.Reset();","\t\t\t(void)payload;",2),
        ("partial-open-leak","\t\tif ( com_journalFile ) fileSystem->CloseFile( com_journalFile );\n\t\tif ( com_journalDataFile ) fileSystem->CloseFile( com_journalDataFile );",
         "\t\t// Deliberately omitted paired-open cleanup.",1),
    ]
    evidence={"passed":False,"platform":platform.platform(),"sources":before,"commands":[],
              "contract_sha256":sha(generated),"scope":"Native historical event-layout decoding and full production event-loop ownership with counted file/dispatch doubles; no cross-ABI journal portability or untrusted-command sandbox claim"}
    log_path=scratch/"test.log"
    with log_path.open("w",encoding="utf-8",newline="\n") as log:
        def run(command:list[str])->subprocess.CompletedProcess[str]:
            result=subprocess.run(command,cwd=ROOT,env=env,text=True,encoding="utf-8",errors="replace",
                                  stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90,check=False)
            log.write(json.dumps(command)+"\n"+result.stdout+f"\nexit_code={result.returncode}\n")
            log.flush()
            evidence["commands"].append({"args":command,"exit_code":result.returncode})
            return result
        try:
            cases=[("production",None)]
            for name,old,new,count in mutations:
                if production.count(old)!=count:
                    raise RuntimeError(f"Mutation anchor count changed: {name}: {production.count(old)} != {count}")
                cases.append((name,production.replace(old,new)))
            for name,source in cases:
                includes=[]
                if source is not None:
                    alternate=scratch/name
                    target=alternate/SOURCES[0]
                    target.parent.mkdir(parents=True)
                    target.write_text(source,encoding="utf-8",newline="\n")
                    metadata=alternate/"src/sys/KeyEventMetadata.h"
                    metadata.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copyfile(ROOT/"src/sys/KeyEventMetadata.h",metadata)
                    shutil.copyfile(ROOT/"src/sys/EventQueueContinuity.h",metadata.with_name("EventQueueContinuity.h"))
                    shutil.copyfile(ROOT/"src/sys/EventDisposition.h",metadata.with_name("EventDisposition.h"))
                    includes=["-I",str(alternate)]
                executable=scratch/(name+"-test"+(".exe" if os.name=="nt" else ""))
                result=run([compiler,"-std=c++17","-O2","-Wall","-Wextra","-Werror",*includes,"-I",str(scratch),"-I",str(ROOT),
                            str(ROOT/SOURCES[3]),str(ROOT/"src/sys/EventQueueContinuity.cpp"),str(ROOT/"src/sys/EventDisposition.cpp"),"-o",str(executable)])
                if result.returncode:
                    raise RuntimeError(f"{name} compilation failed:\n{result.stdout}")
                result=run([str(executable)])
                print(name+": "+result.stdout.strip())
                if name=="production" and result.returncode:
                    raise RuntimeError("Production event journal failed")
                if name!="production" and (result.returncode==0 or "FAIL:" not in result.stdout):
                    raise RuntimeError(f"Mutation did not fail a behavioral assertion: {name}")
            evidence["passed"]=True
            evidence["mutation_count"]=len(mutations)
        except Exception as failure:
            evidence["failure"]=str(failure)
            print(failure)
    evidence["sources_unchanged"]=before=={name:sha(ROOT/name) for name in SOURCES}
    evidence["passed"] &= evidence["sources_unchanged"]
    evidence["log_sha256"]=sha(log_path)
    report=scratch/"result.json"
    report.write_text(json.dumps(evidence,indent=2)+"\n",encoding="utf-8",newline="\n")
    print("Event journal evidence: "+str(report))
    return 0 if evidence["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
