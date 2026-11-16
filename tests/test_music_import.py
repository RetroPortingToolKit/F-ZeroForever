"""Exercise downloaded stock and explicit custom music packs through the real importer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest
import zipfile

TOOL = None
BASE = 'bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2'


def pcm(number):
    return b'MSU1' + struct.pack('<I', 2) + struct.pack('<hh', number * 100, -number * 100) * 8


class MusicImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='fzero-music-test-')
        self.root = Path(self.temp.name)
        self.mods = self.root / 'mods'

    def tearDown(self):
        self.temp.cleanup()

    def archive(self, numbers=range(1, 32), extra=()):
        source = self.root / 'My Soundtrack.zip'
        with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as z:
            for n in numbers:
                z.writestr(f'Downloaded pack/Any Prefix-{n:02d}.PCM', pcm(n))
            for name, data in extra:
                z.writestr(name, data)
        return source

    def run_import(self, source, answers=None, music=False):
        args = [str(TOOL), str(source), str(self.mods), 'My Soundtrack', '', '.']
        if answers is not None or music:
            path = self.root / 'answers.json'
            path.write_text(json.dumps(answers or {}), encoding='utf-8')
            args += [str(path) if answers is not None else '-', 'music' if music else '']
        result = subprocess.run(args, cwd=self.root, capture_output=True, text=True,
                                encoding='utf-8', timeout=60)
        self.assertFalse(any((self.mods / '.imports').glob('*')), result.stderr)
        return result

    def review(self, source, **changes):
        result = self.run_import(source)
        self.assertEqual(result.returncode, 3, result.stderr)
        report = json.loads(Path(re.search(r'Review: (.+)', result.stderr)[1]).read_text())
        values = {f['id']: f['value'] for f in report['review']['fields']}
        values.update(changes)
        return {key: report[key] for key in ('target_sha256', 'input_sha256')} | {'values': values}

    def installed(self):
        folders = list((self.mods / 'packs').iterdir())
        self.assertEqual(len(folders), 1)
        folder = folders[0]
        return folder, json.loads((folder / 'courses.json').read_text())

    def test_expanded_zip_routes_all_original_and_bs_courses_and_events(self):
        source = self.archive(extra=[('cover.jpg', b'cover'), ('setup.exe', b'never run')])
        original = source.read_bytes()
        answers = self.review(source)
        result = self.run_import(source, answers)
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, index = self.installed()
        numbers = [14,6,15,16,10,17,13,8,11,12,19,20,18,21,9]
        ids = [f'{cup}-{i}' for cup in ('knight', 'queen', 'king') for i in range(1, 6)]
        for course, number in zip(ids, numbers):
            self.assertEqual((folder / index['race_music']['retail'][course]).read_bytes(), pcm(number))
        self.assertEqual(len(index['race_music']['bs-deluxe']), 25)
        self.assertEqual(len(index['menu_music']), 6)
        self.assertEqual((folder / index['menu_music']['title']).read_bytes(), pcm(4))
        self.assertEqual(source.read_bytes(), original)
        self.assertFalse(list(folder.rglob('*.exe')))
        self.assertFalse(list(folder.rglob('*.ips')))
        self.assertFalse(list(folder.rglob('*.jpg')))
        again = self.run_import(source, answers)
        self.assertEqual(again.returncode, 1)
        self.assertIn('already installed', again.stderr)

    def test_shared_theme_fallback_and_missing_song(self):
        source = self.archive(numbers=[1,4,6,8,13,14,22])
        result = self.run_import(source, self.review(source))
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, index = self.installed()
        for course in ('knight-1', 'queen-1', 'king-1'):
            self.assertEqual((folder / index['race_music']['retail'][course]).read_bytes(), pcm(14))
        self.assertFalse((folder / index['race_music']['retail']['king-5']).exists())
        self.assertEqual((folder / index['race_music']['bs-deluxe']['bs-2-2']).read_bytes(), pcm(22))

    def test_cgp_layout_is_explicit(self):
        source = self.archive(numbers=range(10, 35))
        result = self.run_import(source, self.review(source, music_layout='cgp'))
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, index = self.installed()
        self.assertEqual((folder / index['race_music']['retail']['knight-1']).read_bytes(), pcm(10))

    def test_bs_fallback_uses_each_courses_native_theme(self):
        source = self.archive(numbers=range(1,17))
        result = self.run_import(source, self.review(source))
        self.assertEqual(result.returncode,0,result.stderr)
        folder, index = self.installed()
        expected = {'bs-1-1':11,'bs-1-3':9,'bs-1-4':12,'bs-2-2':15,
                    'bs-2-3':16,'bs-2-4':8,'bs-2-5':13}
        for course, number in expected.items():
            self.assertEqual((folder / index['race_music']['bs-deluxe'][course]).read_bytes(),pcm(number))

    def test_bs_repeats_prefer_supplied_venue_theme(self):
        source = self.archive(numbers=[*range(1,17),22,24,30])
        result = self.run_import(source,self.review(source))
        self.assertEqual(result.returncode,0,result.stderr)
        folder,index = self.installed()
        for course,number in {'bs-1-4':22,'bs-2-2':22,'bs-2-3':24,'bs-2-5':30}.items():
            self.assertEqual((folder/index['race_music']['bs-deluxe'][course]).read_bytes(),pcm(number))

    def test_music_shortcut_ignores_emulator_patch_and_removal_protects_bundled(self):
        source = self.archive(numbers=[4,14], extra=[('f-zero_msu1.ips', b'not executed'), ('setup.exe', b'not executed')])
        result = self.run_import(source, music=True)
        self.assertEqual(result.returncode, 3, result.stderr)
        report = json.loads(Path(re.search(r'Review: (.+)', result.stderr)[1]).read_text())
        answers = {key: report[key] for key in ('target_sha256','input_sha256')}
        answers['values'] = {f['id']: f['value'] for f in report['review']['fields']}
        result = self.run_import(source, answers, music=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, index = self.installed()
        self.assertFalse(list(folder.rglob('*.ips')))
        (self.mods / '.bundled-packs.json').write_text(json.dumps({'packs':[{'id':index['id']}]}))
        args = [str(TOOL), '--remove', str(self.mods), 'packs/'+folder.name]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertTrue(folder.exists())
        (self.mods / '.bundled-packs.json').unlink()
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(folder.exists())
        self.assertTrue(source.exists())

    def test_source_change_and_bad_pcm_do_not_install(self):
        source = self.archive()
        answers = self.review(source)
        with zipfile.ZipFile(source, 'a') as z:
            z.writestr('Changed-1.pcm', pcm(90))
        result = self.run_import(source, answers)
        self.assertEqual(result.returncode, 1)
        self.assertIn('changed during review', result.stderr)
        self.assertFalse((self.mods / 'packs').exists())

    def test_invalid_loop_is_rejected_before_install(self):
        source = self.archive(numbers=[], extra=[('bad-14.pcm', b'MSU1' + struct.pack('<I', 100) + bytes(8))])
        result = self.run_import(source)
        self.assertEqual(result.returncode, 1)
        self.assertIn('Invalid PCM', result.stderr)
        self.assertFalse((self.mods / 'packs').exists())

    def test_extracted_msu_selects_its_own_set(self):
        source = self.root / 'Chosen.msu'
        source.write_bytes(b'')
        (self.root / 'Chosen-14.pcm').write_bytes(pcm(14))
        (self.root / 'Other-14.pcm').write_bytes(pcm(90))
        result = self.run_import(source, self.review(source))
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, index = self.installed()
        self.assertEqual((folder / index['race_music']['retail']['knight-1']).read_bytes(), pcm(14))

    def test_custom_manifest_maps_original_custom_and_menu_music(self):
        source = self.root / 'custom'
        source.mkdir()
        index = {'format': 1, 'id': 'my-audio', 'name': 'My audio', 'author': 'Test',
                 'race_music': {'retail': {'knight-1': 'race.pcm'},
                                'my-course-pack': {'coast': 'race.pcm'}},
                 'menu_music': {'title': 'race.pcm'}}
        raw = json.dumps(index).encode()
        (source / 'courses.json').write_bytes(raw)
        (source / 'race.pcm').write_bytes(pcm(77))
        (source / 'pack.json').write_text(json.dumps({
            'format': 'snesrecomp.data-pack', 'version': 1, 'game': 'f-zero',
            'id': index['id'], 'title': index['name'], 'base_rom_sha256': BASE,
            'requires': ['fzero-course-v1'], 'payload': {'format': 'fzero.course-index',
                'file': 'courses.json', 'sha256': hashlib.sha256(raw).hexdigest()}}))
        result = self.run_import(source, self.review(source))
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, imported = self.installed()
        self.assertEqual(imported['race_music'], index['race_music'])
        self.assertEqual((folder / 'race.pcm').read_bytes(), pcm(77))

    def test_older_prefix_manifest_gets_numbered_import_review(self):
        source = self.root / 'legacy'
        (source / 'music').mkdir(parents=True)
        (source / 'music/Legacy-14.pcm').write_bytes(pcm(14))
        index = {'format':1,'id':'legacy-audio','name':'Legacy audio','author':'Test',
                 'soundtracks':[{'id':'cgp','prefix':'Legacy','directory':'music'}]}
        raw = json.dumps(index).encode()
        (source / 'courses.json').write_bytes(raw)
        (source / 'pack.json').write_text(json.dumps({
            'format':'snesrecomp.data-pack','version':1,'game':'f-zero',
            'id':index['id'],'title':index['name'],'base_rom_sha256':BASE,
            'payload':{'format':'fzero.course-index','file':'courses.json',
                       'sha256':hashlib.sha256(raw).hexdigest()}}))
        answers = self.review(source)
        self.assertIn('music_layout', answers['values'])
        result = self.run_import(source, answers)
        self.assertEqual(result.returncode,0,result.stderr)
        folder, imported = self.installed()
        self.assertIn('race_music',imported)
        self.assertEqual((folder / imported['race_music']['retail']['knight-1']).read_bytes(),pcm(14))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tool', required=True, type=Path)
    args, rest = parser.parse_known_args()
    TOOL = args.tool.resolve()
    unittest.main(argv=['test_music_import.py', *rest])
