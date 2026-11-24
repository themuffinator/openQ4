#!/usr/bin/env python3
"""Check generated-image alpha with engine screenshots during windowed SP/MP gameplay."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

from PIL import Image
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir",type=Path,default=ROOT / ".install")
    parser.add_argument("--basepath",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--renderer",choices=("gl","vulkan"),default="gl")
    parser.add_argument("--mode",choices=("SP","MP"),default="SP")
    parser.add_argument("--timeout",type=int,default=150)
    args = parser.parse_args()
    runtime,output = args.runtime_dir.resolve(),args.output_dir.resolve()
    executable = "openQ4-client_x64.exe" if os.name=="nt" else "openQ4-client_x64"
    verify_runtime_is_current(runtime,executable)
    output.mkdir(parents=True,exist_ok=False)
    save=output / "save"
    game=save / "baseoq4"
    game.mkdir(parents=True)
    captures=("initial","reopened","restart")
    script=["waitMsec 1500","god","notarget","getviewpos","echo GENERATED_IMAGE_GAMEPLAY_BEGIN"]
    if args.mode=="MP": script.append("openq4_reportMPState")
    for name in captures:
        if name=="restart":
            script += ["vid_restart","waitMsec 1000"]
        script += ["ui_retainedImageProbe 1","waitMsec 500",f"screenshot screenshots/generated/{name}.tga",
                   "ui_retainedImageProbe 0","waitMsec 500"]
    if args.mode=="MP": script.append("openq4_reportMPState")
    script += ["getviewpos","echo GENERATED_IMAGE_GAMEPLAY_END","quit"]
    (game / "generated_probe.cfg").write_text("\n".join(script)+"\n",encoding="utf-8")
    settings={
        "win_allowMultipleInstances" if os.name=="nt" else "sys_allowMultipleInstances":"1",
        "sys_consoleWindow":"0","in_tty":"0","in_mouse":"0","in_joystick":"0",
        "r_fullscreen":"0","r_borderless":"0","r_fullscreenDesktop":"0","r_borderlessDefaultMigrated":"1",
        "r_hiddenWindow":"1","r_mode":"-1","r_customWidth":"640","r_customHeight":"480",
        "r_windowWidth":"640","r_windowHeight":"480","r_renderApi":args.renderer,"r_swapInterval":"0",
        "r_gamma":"1","r_brightness":"1","r_hdrToneMap":"0","com_maxfps":"60","s_noSound":"1",
        "fs_basepath":str(args.basepath.resolve()),"fs_savepath":str(save),"fs_devpath":str(save),
        "fs_game":"baseoq4","com_gameMode":args.mode,"si_gameType":"DM" if args.mode=="MP" else "singleplayer","ui_autoJoin":"1",
        "ui_retained":"1","com_skipLoadingContinue":"1","g_autoSkipCinematics":"1",
        "logFile":"2","logFileName":"logs/openq4.log","developer":"1",
        "g_autoExecAfterMapLoad":"generated_probe.cfg","g_autoExecAfterMapLoadDelayMs":"500",
    }
    if args.mode=="MP":
        settings.update(net_allowCheats="1",sv_cheats="1",net_ip="127.0.0.1",net_enableIPv6="0",net_port="28992",
                        net_serverDedicated="0",net_LANServer="1",si_pure="0",si_maxPlayers="4",bot_minPlayers="0")
    command=[str(runtime / executable)]
    for key,value in settings.items(): command += ["+set",key,value]
    command += ["+spawnServer","mp/q4dm1"] if args.mode=="MP" else ["+map","game/storage1"]
    report={"status":"fail","source":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
            "source_dirty":bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True)),
            "renderer":args.renderer,"mode":args.mode,"command":command,"capture_method":"engine screenshot command",
            "runtime_sha256":{},"samples":{},"failures":[]}
    for path in sorted(runtime.glob("*")):
        if path.is_file() and path.suffix in (".exe",".dll"): report["runtime_sha256"][path.name]=digest(path)
    for path in sorted((runtime / "baseoq4").glob("game_*")):
        if path.is_file(): report["runtime_sha256"]["baseoq4/"+path.name]=digest(path)
    with (output / "console.log").open("w",encoding="utf-8") as stream:
        process=subprocess.Popen(command,cwd=runtime,stdout=stream,stderr=subprocess.STDOUT)
        try:
            report["exit_code"]=process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate();process.wait(timeout=15);report["exit_code"]=process.returncode
            report["failures"].append("gameplay probe timed out")
    if report["exit_code"]!=0: report["failures"].append(f"engine exited {report['exit_code']}")
    log=(game / "logs/openq4.log").read_text(encoding="utf-8",errors="replace")
    for marker in ("GENERATED_IMAGE_GAMEPLAY_BEGIN","GENERATED_IMAGE_GAMEPLAY_END"):
        if marker not in log: report["failures"].append("missing "+marker)
    if log.count("Retained generated-image probe ready:")!=3:
        report["failures"].append("three distinct image allocations were not observed")
    if args.mode=="MP" and len(re.findall(r"^MP_PLAYER slot=0 [^\n]*ingame=1",log,re.M))<2:
        report["failures"].append("local multiplayer player was not in gameplay before and after the probe")
    # Sample texel centres inside each quadrant, away from filtered boundaries.
    # Both rows use a 64-grey background. The second image has a half-alpha tint.
    expected={(48,64):(160,32,32),(112,64):(0,255,0),(48,160):(48,48,112),(112,160):(64,64,64),
              (192,64):(112,48,48),(256,64):(32,160,32),(192,160):(56,56,88),(256,160):(64,64,64)}
    for name in captures:
        path=game / "screenshots/generated" / (name+".tga")
        if not path.is_file(): report["failures"].append("missing "+str(path));continue
        with Image.open(path) as source:
            image=source.convert("RGB")
            image.save(output / (name+".png"))
            report["samples"][name]={"sha256":digest(path),"size":list(image.size),"pixels":{}}
            if image.size!=(640,480):
                report["failures"].append(name+" unexpected capture dimensions");continue
            for point,wanted in expected.items():
                actual=image.getpixel(point)
                report["samples"][name]["pixels"][str(point)]=actual
                if max(abs(a-b) for a,b in zip(actual,wanted))>3:
                    report["failures"].append(f"{name} {point}: {actual}, expected {wanted} within 3")
    report["status"]="fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({key:report[key] for key in ("status","exit_code","failures")},indent=2))
    return int(bool(report["failures"]))


if __name__=="__main__":
    raise SystemExit(main())
