#!/usr/bin/env python3
"""Validate the retained HUD catalog, and optionally audit installed GUI/def assets.

--assets and --awakening accept extracted content directories containing guis/.
The optional asset audit reads reference files only; it never embeds retail art.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / 'content/baseoq4/pak0/guis/hud'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--awakening', type=Path)
    args = parser.parse_args()
    subprocess.run([sys.executable, str(ROOT/'tools/ui/build_retained_hud.py'), '--check'], cwd=ROOT, check=True)
    registry = json.loads((CATALOG/'coverage.json').read_text())
    catalog = json.loads((CATALOG/'artwork.q4ui').read_text())
    nodes = {n['id']: n for n in catalog['root']['children']}
    own = registry.get('openq4Roots', [])
    roots = registry['roots'] + registry['expansionRoots'] + own
    assert len(set(roots)) == len(roots)
    for material, art in registry['materials'].items():
        assert material == material.lower().replace('\\', '/')
        assert art in nodes and nodes[art]['paths'], (material, art)
    assert set(registry['images']).isdisjoint(registry['materials'])
    assert set(registry['imageAliases']) <= set(registry['images'])
    # Side weapon images must resolve to real source bitmaps, never the
    # catalog's synthetic colored compatibility spellings.
    for name in registry['images']:
        if '/gun_' not in name:
            continue
        target = registry['imageAliases'].get(name, name)
        assert '_colored' not in target, (name, target)
        supplied = [root for root in (args.assets, args.awakening) if root]
        optional = target.endswith(('gun_corecannon', 'gun_freezegun', 'gun_flamegun')) and not args.awakening
        if supplied and not optional:
            assert any((root/(target+'.tga')).is_file() for root in supplied), (name, target)
    # The fixed route allowlist must cover exactly the audited root families.
    adapter = (ROOT/'src/ui/RetainedHud.cpp').read_text()
    eligible = adapter.split('static const std::set<std::string> roots={', 1)[1].split('};', 1)[0]
    assert set(re.findall(r'"(guis/[^\"]+\.gui)"', eligible)) == set(roots)
    missing = []
    files = 0
    def audit(path):
        nonlocal files
        files += 1
        source = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(errors='replace'), flags=re.S)
        for material in set(re.findall(r'(?i)\b(?:gfx/guis/|gfx/mp/|gfx/effects/particles_shapes/blank)[\w./-]+', source)):
            name = material.lower()
            if name not in registry['materials'] and name not in registry['images']:
                missing.append((str(path), name))
    # HUDs openQ4 ships itself are audited from the repository, every run.
    for root in own:
        path = ROOT/'content/baseoq4/pak0'/root
        assert path.is_file(), path
        audit(path)
    for assets, family in ((args.assets, registry['roots']), (args.awakening, registry['expansionRoots'])):
        if assets:
            for root in family:
                path = assets/root
                assert path.is_file(), path
            # Expansion packs can override retail-named HUDs as well as add
            # new roots. Audit those effective definitions too.
            for root in roots:
                path = assets/root
                if path.is_file():
                    audit(path)
            # Dynamic item/weapon icons do not occur in the HUD GUI source.
            for path in (assets/'def').rglob('*.def'):
                text = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(errors='replace'), flags=re.S)
                for match in re.finditer(r'"(?:gui_hud|hud|mphud|cursor|wristcomm)"\s+"([^\"]+\.gui)"', text):
                    name = match[1].lower()
                    # Some unused retail test entities reference absent GUIs.
                    # Only a supplied HUD definition creates a migration route.
                    supplied = (assets/name).is_file() or (args.assets and (args.assets/name).is_file())
                    if supplied and name not in roots:
                        missing.append((str(path), 'HUD root: '+name))
                for match in re.finditer(r'"(?:inv_icon|inv_ammoicon|inv_weaponicon|hud_icon|mtr_icon)"\s+"([^\"]+)"', text):
                    name = match[1].lower()
                    if name.startswith('gfx/guis/hud/') and name not in registry['materials'] and name not in registry['images']:
                        missing.append((str(path), name))
    assert not missing, '\n'.join(f'{p}: {name}' for p, name in sorted(set(missing)))
    print(f'HUD coverage: {len(roots)} routes, {len(nodes)} editable symbols, {len(registry["materials"])} aliases; {files} installed GUIs audited')


if __name__ == '__main__':
    main()
