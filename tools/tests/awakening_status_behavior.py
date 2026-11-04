#!/usr/bin/env python3
"""Compile production Awakening status/targeting methods against counted test doubles.

No game assets, game launch, desktop capture or input injection are required.
The generated projection, compiler output and result stay under project .tmp.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

from awakening_gameplay_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    'src/game/Actor.cpp': ['void idActor::Damage('],
    'src/game/ai/AI.cpp': [
        'void idAI::AdjustHealthByDamage', 'bool idAI::IsFrozenSolid',
        'void idAI::SetFreezeFactor', 'void idAI::SetFreezeOverlay',
        'void idAI::UpdateStatusOverlay', 'bool idAI::ApplyRetchBuff',
        'void idAI::ClearRetchBuff', 'void idAI::SaveAwakeningState',
        'void idAI::RestoreAwakeningState', 'void idAI::AddFreeze',
        'void idAI::FreezeSolid', 'void idAI::UpdateFreeze'],
    'src/game/anim/Anim_Blend.cpp': ['void idAnimator::SetPlaybackRateScale('],
    'src/game/awakening/ai/Monster_PainLord.cpp': ['void riMonsterPainLord::AdjustHealthByDamage(',
        'bool riMonsterPainLord::DudeAttached(', 'void riMonsterPainLord::DetachDude(',
        'void riMonsterPainLord::DamageDude(', 'int riMonsterPainLord::GetDamageForLocation(',
        'void riMonsterPainLord::Enrage(', 'void riMonsterPainLord::Event_AtProcessingStation(',
        'stateResult_t riMonsterPainLord::State_Torso_Stun(',
        'stateResult_t riMonsterPainLord::State_Torso_ProcessingStation('],
    'src/game/awakening/ai/Monster_Valkaryne.cpp': ['bool riMonsterValkaryne::DamageProtected(',
        'void riMonsterValkaryne::AdjustHealthByDamage('],
    'src/game/awakening/ai/Monster_Retch.cpp': [
        'bool riMonsterRetch::ApplyBuff(', 'void riMonsterRetch::ClearBuff(',
        'bool riMonsterRetch::FindBuffTarget(', 'void riMonsterRetch::Event_RestoreBuff('],
    'src/game/awakening/SpaceRocket.cpp': [
        'idVec3 AwakeningProjectileTargetPoint(', 'void riProjectileSpaceRocket::SetTarget(',
        'void riProjectileSpaceRocket::SetTargetPosition(', 'void riProjectileSpaceRocket::Think('],
    'src/game/awakening/vehicle/CockpitWeapons.cpp': [
        'idActor *riVehicleCockpitWeapon::FindEnemyNearCrosshair(',
        'void riVCWMissileTurret::SetLockState(', 'void riVCWMissileTurret::UpdateTargeting(',
        'void riVCWMissileTurret::SetRocketsLoaded(', 'void riVCWMissileTurret::UpdateMagazine(',
        'bool riVCWMissileTurret::Fire('],
    'src/game/weapon/WeaponNapalmGun.cpp': ['stateResult_t WeaponNapalmGun::State_Idle('],
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'.tmp/awakening-status-behavior')
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT/'.tmp').resolve()):
        parser.error('output must be inside project .tmp')
    for parent in (output, *output.parents):
        if parent == ROOT:
            break
        if parent.is_symlink() or (parent.exists() and getattr(parent.stat(follow_symlinks=False), 'st_file_attributes', 0) & 0x400):
            parser.error(f'output traverses a link: {parent}')
    output.mkdir(parents=True, exist_ok=True)
    sources = {name: (ROOT/name).read_text(encoding='utf-8-sig') for name in SOURCES}
    methods = []
    for name, signatures in SOURCES.items():
        for signature in signatures:
            body = function_body(sources[name], signature, name)
            # The contract helper returns the body, so retain the real
            # signature as well; C++ compilation detects drift from the doubles.
            start = sources[name].index(signature)
            opening = sources[name].index('{', start)
            methods.append(sources[name][start:opening] + '{' + body + '}')
    mp = (ROOT/'src/mpgame/weapon/WeaponNapalmGun.cpp').read_text()
    assert function_body(mp, SOURCES['src/game/weapon/WeaponNapalmGun.cpp'][0], 'MP napalm').replace('if(!clipSize)', 'if(!ClipSize())') == function_body(
        sources['src/game/weapon/WeaponNapalmGun.cpp'], SOURCES['src/game/weapon/WeaponNapalmGun.cpp'][0], 'SP napalm')
    # Existing v3 byte encodings must remain selected after the writer bumps.
    for tree in ('game', 'mpgame'):
        for path in (ROOT/'src'/tree).rglob('*.cpp'):
            assert 'GetOpenQ4SaveGameCompatibilityVersion() == OPENQ4_SAVEGAME_COMPATIBILITY_VERSION' not in path.read_text(encoding='utf-8-sig'), path
    fixture = ROOT/'tools/tests/native/AwakeningStatusBehavior.cpp'
    constants = ['static const float PAINLORD_ENRAGED_ANIM_RATE = 1.75f;']
    for source in sources.values():
        constants += re.findall(r'static const [^;\n]*\b(?:AI_FR(?:EEZ|OZ)\w*|SPACEROCKET_\w*|COCKPIT_TARGET_CONE|RETCH_\w+)\s*=[^;]*;', source)
    constants = [line for line in constants if 'RETCH_BEAM_RANGE' not in line and 'RETCH_FALLBACK_AAS' not in line]
    code = fixture.read_text().replace('// @PRODUCTION_CONSTANTS@', '\n'.join(constants))
    code = code.replace('// @PRODUCTION_METHODS@', '\n\n'.join(methods))
    projection = output/'behavior.cpp'
    projection.write_text(code, encoding='utf-8')
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ required')
    binary = output/('behavior.exe' if os.name == 'nt' else 'behavior')
    env = {**os.environ, 'TMP': str(output), 'TEMP': str(output), 'TMPDIR': str(output)}
    warnings = ['-Wno-bitwise-instead-of-logical'] if 'clang' in Path(compiler).name else []
    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *warnings,
                            str(projection), '-o', str(binary)], cwd=output, env=env,
                           text=True, capture_output=True)
    (output/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode:
        print(build.stdout+build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary)], cwd=output, env=env, text=True, capture_output=True)
    (output/'run.log').write_text(run.stdout+run.stderr)
    (output/'result.json').write_text(json.dumps({
        'passed': run.returncode == 0, 'output': run.stdout+run.stderr,
        'sources': {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCES},
        'projection_sha256': hashlib.sha256(projection.read_bytes()).hexdigest()}, indent=2)+'\n')
    print(run.stdout+run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
