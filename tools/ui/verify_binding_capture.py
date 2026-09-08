#!/usr/bin/env python3
"""Check binding state traces and gauge pixels from an engine target capture."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def verify(folder: Path) -> dict:
    capture = json.loads((folder/'capture.json').read_text())
    preview = capture['retained_preview']
    if capture['status'] != 'captured_pending_visual_review' or preview['diagnostics']:
        return {'passed':False,'reason':'capture or retained runtime reported errors'}
    def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
    fixture = ROOT/'tools/ui/fixtures'
    source_ok = preview['sha256'] == sha(fixture/'binding-smoke.q4ui')
    source_ok &= preview['interaction_script']['sha256'] == sha(fixture/'binding-smoke.cfg')
    data_hashes = {row['name']:row['sha256'] for row in preview['state_data']}
    source_ok &= data_hashes == {name:sha(fixture/name) for name in ('binding-first.json','binding-final.json')}
    log_path = folder/'save/baseoq4/logs/openq4.log'
    source_ok &= capture['log_sha256'] == sha(log_path)
    image_path = folder/capture['screenshot']['path']
    source_ok &= capture['screenshot']['sha256'] == sha(image_path)
    if not source_ok:
        return {'passed':False,'reason':'fixture, log, data or screenshot provenance changed'}
    log = re.sub(r'\^[0-9]','',log_path.read_text(encoding='utf-8',errors='replace'))
    trace = [line for line in log.splitlines() if line.startswith(('Retained UI value:','Retained UI data:','Retained UI bounds:'))]
    if trace != preview['binding_trace']:
        return {'passed':False,'reason':'capture binding trace differs from engine log'}
    repeats = 2 if preview['video_restart'] else 1
    readings = [line.split('=',1)[1] for line in trace if line.startswith('Retained UI value: reading.text=')]
    scales = [line.split('=',1)[1] for line in trace if line.startswith('Retained UI value: scale-reading.text=')]
    widths = [line.split('=',1)[1] for line in trace if line.startswith('Retained UI value: progress-fill.width=')]
    batches = [int(line.rsplit(' ',1)[1]) for line in log.splitlines() if line.startswith('Retained UI actions:')]
    actions = [line for line in log.splitlines() if line.startswith('Retained UI action:')]
    expected_action = 'Retained UI action: activate document=binding-qualification node=reference-controls action=menu.controls'
    states = re.findall(r'^Retained UI control: reference-controls state=(\w+)',log,re.M)
    behavior = (readings == ['25','40','75']+(['75','40','75'] if repeats == 2 else []) and
                scales == [f'{preview["ui_scale"]:.2f}']*repeats and widths == ['75%']*repeats and
                batches == [0,1]*repeats and actions == [expected_action]*repeats and states == ['disabled','focus']*repeats)
    bounds = {}
    for line in trace:
        match = re.fullmatch(r'Retained UI bounds: (\S+)=([0-9.,-]+)',line)
        if match:
            bounds[match[1]] = [float(v) for v in match[2].split(',')]
    if not behavior or 'progress-fill' not in bounds or 'progress-track' not in bounds:
        return {'passed':False,'behavior':False,'readings':readings,'scales':scales,'widths':widths,'batches':batches,'states':states}
    track, fill = bounds['progress-track'],bounds['progress-fill']
    layout = abs(fill[2]-track[2]*.75) < .2 and abs(fill[0]-track[0]) < .2 and abs(fill[1]-track[1]) < .2
    image = Image.open(image_path).convert('RGB')
    def patch(x0,x1):
        pixels=[]
        for y in range(round(track[1]+track[3]*.25),round(track[1]+track[3]*.7)):
            for x in range(round(track[0]+track[2]*x0),round(track[0]+track[2]*x1)):
                if 0 <= x < image.width and 0 <= y < image.height:
                    pixels.append(image.getpixel((x,y)))
        return pixels
    filled, empty = patch(.2,.6),patch(.82,.95)
    orange = sum(r>190 and 110<g<160 and b<35 for r,g,b in filled)
    olive = sum(45<r<85 and 50<g<95 and 20<b<65 for r,g,b in empty)
    result = {'passed':layout and bool(filled) and bool(empty) and orange/len(filled)>.99 and olive/len(empty)>.99,
              'behavior':behavior,'layout':layout,'readings':readings,'cvar_values':scales,'action_batches':batches,
              'filled_pixels':len(filled),'orange_pixels':orange,'empty_pixels':len(empty),'olive_pixels':olive,
              'track_bounds':track,'fill_bounds':fill,'replacement_acceptance':False}
    return result


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('capture',type=Path)
    cli.add_argument('--output',type=Path)
    args = cli.parse_args()
    result = verify(args.capture)
    text = json.dumps(result,indent=2)+'\n'
    if args.output:
        args.output.write_text(text,encoding='utf-8')
    print(text)
    raise SystemExit(0 if result['passed'] else 1)
