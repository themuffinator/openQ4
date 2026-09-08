#!/usr/bin/env python3
"""Exercise inventory against real temporary ZIP/VFS and lexical adversaries."""

from __future__ import annotations

from functools import cmp_to_key
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ui'))
from legacy_inventory import Catalog, LexError, compare_paks, inventory, lex, migration_seed, qpath, main
import legacy_import


class InventoryTests(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(__file__).resolve().parents[2] / '.tmp'
        temporary_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='ui-inventory-', dir=temporary_root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding='utf-8')

    def pack(self, name, files):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with ZipFile(path, 'w') as archive:
            for key, value in files.items():
                archive.writestr(key, value)

    def test_package_order_and_loose_override(self):
        for name, window in [('pak001.pk4', 'retail'), ('pak01.pk4', 'medium'),
                             ('pak1.pk4', 'short'), ('zmod.pk4', 'mod')]:
            self.pack(name, {'guis/main.gui': f'windowDef {window} {{}}'})
        catalog = Catalog()
        catalog.mount('base', self.root)
        self.assertEqual(inventory(catalog)['resources'][0]['windows'][0]['name'], 'mod')
        self.write('guis/main.gui', 'windowDef loose {}')
        catalog = Catalog()
        catalog.mount('base', self.root)
        report = inventory(catalog)
        self.assertEqual(report['resources'][0]['windows'][0]['name'], 'loose')
        self.assertEqual(len(report['source_versions_low_to_high']['guis/main.gui']), 5)
        self.assertEqual(sorted(['pak1.pk4', 'pak002.pk4', 'pak001.pk4', 'pak10.pk4'],
                                key=cmp_to_key(compare_paks)),
                         ['pak001.pk4', 'pak002.pk4', 'pak10.pk4', 'pak1.pk4'])

    def test_mount_priority_is_explicit(self):
        self.pack('stock/pak001.pk4', {'guis/Main.gui': 'windowDef stock {}'})
        self.write('override/guis/main.gui', 'windowDef replacement {}')
        catalog = Catalog()
        catalog.mount('stock', self.root / 'stock')
        catalog.mount('override', self.root / 'override')
        self.assertEqual(inventory(catalog)['resources'][0]['windows'][0]['name'], 'replacement')

    def test_same_archive_case_collision_is_error(self):
        self.pack('pak0.pk4', {'guis/A.gui': 'windowDef A {}', 'guis/a.gui': 'windowDef B {}'})
        catalog = Catalog()
        catalog.mount('base', self.root)
        self.assertEqual(catalog.diagnostics[0]['code'], 'ambiguous_case_or_duplicate')

    def test_comments_strings_and_source_positions(self):
        source = '// windowDef fake {}\nwindowDef Real {\n text "onTime 0 { // not syntax }"\n'
        source += ' /* onNamedEvent false {} */ onNamedEvent open { set "gui::x" "#str_1"; }\n}'
        self.write('guis/a.gui', source)
        catalog = Catalog()
        catalog.mount('base', self.root)
        result = inventory(catalog)['resources'][0]
        self.assertEqual([w['name'] for w in result['windows']], ['Real'])
        self.assertEqual(result['windows'][0]['line'], 2)
        self.assertEqual(result['windows'][0]['column'], 1)
        self.assertEqual(result['events'][0]['argument'], 'open')
        self.assertEqual(result['script_commands'], {'set': 1})
        self.assertEqual(result['localization_keys'], ['#str_1'])
        self.assertEqual(result['state_references'], ['gui::x'])

    def test_relative_and_root_includes_and_dependency_order(self):
        self.write('guis/main.gui', 'windowDef Main { #include "parts/body.inc" }')
        self.write('guis/parts/body.inc', '#include "guis/shared.inc"\nwindowDef Body {}')
        self.write('guis/shared.inc', 'windowDef Shared {}')
        catalog = Catalog()
        catalog.mount('base', self.root)
        report = inventory(catalog)
        self.assertEqual(report['include_dependency_order'],
                         ['guis/shared.inc', 'guis/parts/body.inc', 'guis/main.gui'])
        self.assertFalse(report['diagnostics'])
        seed = migration_seed(report)
        self.assertEqual(len(seed['resources']), 3)
        self.assertTrue(all(r['replacement'] is None and not r['family_reviewed'] for r in seed['resources']))

    def test_window_names_that_begin_with_on_are_not_events(self):
        self.write('guis/a.gui', 'windowDef Desktop { windowDef online { visible 1 } windowDef one { rect 1,2,3,4 } }')
        catalog = Catalog()
        catalog.mount('base', self.root)
        result = inventory(catalog)['resources'][0]
        self.assertEqual(len(result['windows']), 3)
        self.assertFalse(result['events'])
        self.assertFalse(result['script_commands'])

    def test_window_names_that_match_properties_are_not_asset_references(self):
        self.write('guis/a.gui', 'windowDef background {} windowDef font {} windowDef model {}')
        catalog = Catalog()
        catalog.mount('base', self.root)
        result = inventory(catalog)['resources'][0]
        self.assertEqual(len(result['windows']), 3)
        self.assertFalse(result['asset_references'])

    def test_event_names_that_match_properties_are_not_asset_references(self):
        self.write('guis/a.gui', 'windowDef D { onNamedEvent background { set "gui::x" 1; } }')
        catalog = Catalog()
        catalog.mount('base', self.root)
        result = inventory(catalog)['resources'][0]
        self.assertEqual(result['events'][0]['argument'], 'background')
        self.assertFalse(result['asset_references'])

    def test_cycle_missing_include_and_no_fake_syntax(self):
        self.write('guis/a.gui', '#include "b.gui"\n#include "missing.inc"')
        self.write('guis/b.gui', '#include "a.gui"')
        catalog = Catalog()
        catalog.mount('base', self.root)
        report = inventory(catalog)
        codes = {d['code'] for d in report['diagnostics']}
        self.assertEqual(codes, {'include_cycle', 'missing_include'})

    def test_invalid_qpaths_and_lexical_errors(self):
        for bad in ('../guis/a.gui', 'C:/a.gui', '/a.gui'):
            with self.assertRaises(ValueError):
                qpath(bad)
        for bad in ('/* never ends', '"unfinished', '"escape\\'):
            with self.assertRaises(LexError):
                lex(bad)
        tokens = lex('text "a\\\"b" /* comment */ rect 1,2,3,4')
        self.assertEqual(tokens[1].value, 'a\\\"b')

    def test_binary_game_archives_are_ignored_only_in_q4base(self):
        self.pack('q4base/game000.pk4', {'guis/ignored.gui': 'windowDef ignored {}'})
        self.pack('q4base/pak001.pk4', {'guis/kept.gui': 'windowDef kept {}'})
        catalog = Catalog()
        catalog.mount('retail', self.root / 'q4base')
        self.assertEqual(inventory(catalog)['summary']['gui_files'], 1)
        self.assertEqual(catalog.mounts[0]['ignored_archives'], ['game000.pk4'])

    def test_materials_images_and_runtime_bindings_remain_distinct(self):
        self.write('guis/a.gui', 'windowDef A { background "gfx/plate-cut" }\n'
                   'windowDef B { background "gui::image" }\n'
                   'windowDef C { background "gfx/photo" }\n'
                   'windowDef D { background "missing" }')
        self.write('materials/gui.mtr', '/* fake {} */\ngfx/plate-cut { { map gfx/plate-cut.tga } }')
        self.write('gfx/photo.tga', 'not decoded for inventory')
        catalog = Catalog()
        catalog.mount('base', self.root)
        by_value = {d['value']: d for d in inventory(catalog)['asset_dependencies']}
        self.assertEqual(by_value['gfx/plate-cut']['resolution'], 'declared_material')
        self.assertEqual(by_value['gui::image']['resolution'], 'runtime_binding')
        self.assertEqual(by_value['gfx/photo']['file_candidates'], ['gfx/photo.tga'])
        self.assertEqual(by_value['missing']['resolution'], 'unresolved_requires_review')
        self.assertTrue(all(d['art_classification'] == 'pending' for d in by_value.values()))

    def test_duplicate_materials_are_not_silently_resolved(self):
        self.write('guis/a.gui', 'windowDef A { background "gfx/plate" }')
        self.write('materials/one.mtr', 'gfx/plate { { map _white } }')
        self.write('materials/two.mtr', 'gfx/plate { { map _black } }')
        catalog = Catalog()
        catalog.mount('base', self.root)
        item = inventory(catalog)['asset_dependencies'][0]
        self.assertEqual(item['resolution'], 'multiple_declaration_candidates')
        self.assertEqual(len(item['declaration_candidates']), 2)

    def test_native_export_requests_use_effective_source_hashes(self):
        self.pack('pak001.pk4', {'guis/a.gui':'windowDef A {}'})
        self.write('guis/a.gui','windowDef Override {}')
        report = self.root/'report.json'
        requests = self.root/'requests.json'
        with redirect_stdout(io.StringIO()):
            code = main(['--mount',f'base={self.root}','--output',str(report),'--export-requests',str(requests)])
        self.assertEqual(code,0)
        resource = json.loads(report.read_text())['resources'][0]
        self.assertEqual(legacy_import.requests(requests),[{'path':resource['path'],'sha256':resource['sha256']}])


if __name__ == '__main__':
    unittest.main()
