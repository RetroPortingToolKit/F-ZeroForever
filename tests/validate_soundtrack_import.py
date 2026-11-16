"""Muted private-ROM playback of an actually imported numbered soundtrack.

Checks all original/BS course routes, SPC fallback, local simulation neutrality
and exact current-build audio save/load through the existing host test hooks.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import wave
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'out'):
        parser.add_argument('--' + key, required=True, type=Path)
    args = parser.parse_args()
    build, rom, out = (getattr(args, k).resolve() for k in ('build', 'rom', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    shutil.copytree(root / 'assets', out / 'assets')
    archive = out / 'Downloaded soundtrack.zip'
    with zipfile.ZipFile(archive, 'w') as z:
        for number in range(1, 32):
            z.writestr(f'Original music/stock-{number}.pcm', b'MSU1' + bytes(4) +
                       struct.pack('<hh', number*100, -number*100)*4410)
    mods = out / 'mods'
    command = [str(build / 'FZeroImportContent.exe'), str(archive), str(mods), 'Imported stock music', '', '.']
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert result.returncode == 3, result.stderr
    report = json.loads(Path(re.search(r'Review: (.+)', result.stderr)[1]).read_text())
    answers = {k: report[k] for k in ('target_sha256', 'input_sha256')}
    answers['values'] = {f['id']: f['value'] for f in report['review']['fields']}
    answer_path = out / 'answers.json'
    answer_path.write_text(json.dumps(answers))
    result = subprocess.run(command + [str(answer_path)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    soundtrack = next((mods / 'packs').iterdir())
    selected = soundtrack.name
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'SNES_RB', 'RNET_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                 FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='0', FZERO_BS_CARS='0',
                 FZERO_RULES='', FZERO_PACK_LOADER='0', FZERO_PACKS_DIR=str(mods / 'packs'))

    def run(name, course=0, on=True, state=False, single=False):
        folder = out / name
        folder.mkdir()
        settings = folder / 'settings'
        settings.mkdir()
        (settings / 'loader.cfg').write_text('enabled=0\nsoundtrack='+selected+'\n')
        env = dict(clean, FZERO_TRACK_PACKS=str(settings), FZERO_BS_TRACKS='1' if course >= 15 else '0',
                   SNESRECOMP_MSU1=str(mods / 'packs/installed-music') if on else 'off',
                   SNESRECOMP_SAVE_ROOT=name+'/s', SNESRECOMP_WAV=str(folder / 'audio.wav'),
                   FZERO_TEST_APURAM_DUMP=str(folder / 'apu.bin'), FZERO_TEST_DSP_DUMP=str(folder / 'dsp.bin'),
                   SNESRECOMP_WRAM_DUMP=str(folder / 'ram.bin'))
        if state:
            env['FZERO_TEST_MSU_STATE'] = str(folder / 'music.state')
        cmd = [str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), '1100', '--versus', str(course), '9', '3']
        if single:
            env.update(FZERO_RULES='cgp-msu', SNESRECOMP_INPUT_SCRIPT=
                       '320-326:8,800-806:8,920-926:8,1090-1096:8,1150-1156:8,1520-2199:1')
            cmd = cmd[:2] + ['2200']
        result = subprocess.run(cmd, cwd=out, env=env, capture_output=True, text=True, timeout=180)
        log = result.stdout + result.stderr
        (folder / 'run.log').write_text(log, encoding='utf-8')
        assert result.returncode == 0, (name, result.returncode, log[-3000:])
        with wave.open(str(folder / 'audio.wav')) as audio:
            pairs = list(struct.iter_unpack('<hh', audio.readframes(audio.getnframes())))
        counts = Counter(l-r for l,r in pairs[-32040:])
        if state:
            assert 'msu-state: PASS' in log, (name, log[-3000:])
        return folder, log, counts

    numbers = [14,6,15,16,10,17,13,8,11,12,19,20,18,21,9,22,23,24,25,26,27,28,29,30,31]
    rows = []
    for course, number in enumerate(numbers):
        folder, log, counts = run(f'course-{course:02d}', course)
        assert 'output=PCM' in log and counts[number*200] > 1000, (course, number, counts.most_common(4), log[-1000:])
        rows.append({'course':course, 'number':number, 'passed':True})
        print(f'course {course}: imported PCM {number} PASS', flush=True)
    native, _, _ = run('native-reference', on=False)
    for leaf in ('ram.bin', 'apu.bin', 'dsp.bin'):
        assert (native / leaf).read_bytes() == (out / 'course-00' / leaf).read_bytes(), leaf
    # A missing selected recording must use SPC, even with a valid soundtrack selected.
    (soundtrack / 'music/track-14.pcm').rename(soundtrack / 'music/track-14.unavailable')
    folder, log, counts = run('missing-song')
    assert 'output=SPC' in log and any(k for k in counts), log[-1500:]
    for leaf in ('ram.bin', 'apu.bin', 'dsp.bin'):
        assert (native / leaf).read_bytes() == (folder / leaf).read_bytes(), leaf
    # Nonconstant PCM and a nonzero loop point make playback-position tests meaningful.
    samples = b''.join(struct.pack('<hh', (i*37 % 30001)-15000, (i*61 % 29003)-14500) for i in range(220500))
    (soundtrack / 'music/track-14.pcm').write_bytes(b'MSU1' + struct.pack('<I', 1197) + samples)
    run('selected-save-load', state=True)
    run('single-player-save-load', state=True, single=True)
    (out / 'report.json').write_text(json.dumps({'routes':rows, 'canonical_neutrality':True,
        'missing_song_spc':True, 'save_load_rewind':True}, indent=2))
    print('Imported soundtrack playback, fallback, simulation neutrality and save/load PASS', flush=True)


if __name__ == '__main__':
    main()
