#!/usr/bin/env python3
"""Qualify the semantic trace and selected rendered states of interaction-smoke.

Reads capture metadata and the engine screenshot. It never accesses OS input
or a desktop image. This is button/scope qualification, not GUI migration parity.
"""
import argparse
import json
from pathlib import Path
import re
from PIL import Image


def verify(folder: Path) -> dict:
    capture = json.loads((folder/'capture.json').read_text())
    trace = capture['retained_preview']['interaction_trace']
    repetitions = 2 if capture['retained_preview']['video_restart'] else 1
    counts = [int(line.split(': ')[1]) for line in trace if line.startswith('Retained UI actions:')]
    expected_action = 'Retained UI action: activate document=interaction-qualification node=reference-controls action=menu.controls'
    actions = [line for line in trace if line.startswith('Retained UI action:')]
    observed = []
    bounds = {}
    for line in trace:
        match = re.fullmatch(r'Retained UI control: (\S+) state=(\S+) focus=(\S*) bounds=([0-9.,-]+)',line)
        if match:
            node, state, focused, rect = match.groups()
            observed.append([node,state,focused])
            bounds[node] = [float(v) for v in rect.split(',')]
    expected = [['reference-system','focus','reference-system'],['modal-controls','focus','modal-controls'],
                ['reference-game-options','focus','reference-game-options'],['reference-game-options','disabled',''],
                ['modal-game-options','pressed','modal-game-options']]
    behavioral = counts == [0,1,0]*repetitions and actions == [expected_action]*repetitions and observed == expected*repetitions
    if not behavioral:
        return {'passed':False,'behavioral':False,'counts':counts,'actions':actions,'states':observed}
    image = Image.open(folder/capture['screenshot']['path']).convert('RGB')
    density = capture['retained_preview']['density_override']*capture['retained_preview']['ui_scale']
    left, right = bounds['reference-game-options'], bounds['modal-game-options']
    # Compare a uniform plate patch clear of labels, markers and inset rails.
    backdrop = image.getpixel((round(left[0]-8*density),round(left[1]+20*density)))
    errors = []
    for y in range(round(30*density),round(33*density)):
        for x in range(round(280*density),round(330*density)):
            disabled = image.getpixel((round(left[0])+x,round(left[1])+y))
            pressed = image.getpixel((round(right[0])+x,round(right[1])+y))
            errors.append(max(abs(disabled[i]-(pressed[i]*.35+backdrop[i]*.65)) for i in range(3)))
    orange = 0
    for y in range(round(right[1]),round(right[1]+right[3])):
        for x in range(round(right[0]),round(right[0]+right[2])):
            r,g,b = image.getpixel((x,y))
            # Include partially covered pixels of the analytic-AA orange rail.
            orange += r > 150 and 90 < g < 190 and b < 50
    result = {'behavioral':True,'repetitions':repetitions,'action_batches':counts,'activations':len(actions),
              'state_observations':len(observed),'disabled_patch_pixels':len(errors),'disabled_patch_max_error':max(errors),
              'pressed_orange_rail_pixels':orange,'backdrop':backdrop}
    result['passed'] = max(errors) <= 3 and orange > 100*density
    if capture['retained_preview'].get('application_open'):
        ownership = []
        for line in capture['retained_preview']['ownership_trace']:
            match = re.fullmatch(r'Retained UI ownership: open=(\d) suspended=(\d) session_gui=(\d) game_time=(-?\d+) requests=(\d+)',line)
            if match:
                ownership.append([int(value) for value in match.groups()])
        valid = len(ownership) == repetitions*2+1
        if valid:
            valid = all(row[0] == 1 and row[2] == 1 and row[3] >= 0 for row in ownership[:-1])
            valid = valid and ownership[-1][0] == 0 and ownership[-1][2] == 0
            for begin,end in zip(ownership[:-1:2],ownership[1:-1:2]):
                valid = valid and (end[3] == begin[3] if capture['mode'] == 'sp' else end[3] > begin[3])
            valid = valid and ownership[-1][3] > ownership[-2][3]
        result['application_ownership'] = ownership
        result['ownership_passed'] = valid
        result['passed'] = result['passed'] and valid
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    result = verify(parser.parse_args().capture)
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['passed'] else 1)
