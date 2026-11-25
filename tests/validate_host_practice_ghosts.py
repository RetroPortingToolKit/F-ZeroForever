"""Exercise recording and rival selection on the retail HLE build."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "stock", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    build, stock, out = (path.resolve() for path in (args.build, args.stock, args.out))
    out.mkdir(parents=True, exist_ok=False)
    executable = build / ("FZeroSNESRecompHeadless.exe" if os.name == "nt"
                          else "FZeroSNESRecompHeadless")
    original = hashlib.sha256(stock.read_bytes()).hexdigest()
    env = {name: value for name, value in os.environ.items()
           if not name.startswith(("FZERO_", "SNESRECOMP_", "SDL_", "LNG_"))}
    env.update(SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy",
               FZERO_PACK_LOADER="0", FZERO_GHOSTS="1", FZERO_ALWAYS_RECORDS="1",
               SNESRECOMP_SAVE_ROOT="s", FZERO_TEST_SAVE_SRAM="1")

    def run(name, script, frames, **options):
        log = out / f"{name}.log"
        frame = out / f"{name}.ppm"
        ram = out / f"{name}.ram"
        settings = dict(env, SNESRECOMP_INPUT_SCRIPT=script,
                        SNESRECOMP_WRAM_DUMP=str(ram), SNESRECOMP_FRAME_DUMP=str(frame),
                        **options)
        with log.open("wb") as output:
            process = subprocess.run([str(executable), str(stock), str(frames)],
                                     cwd=out, env=settings, stdout=output,
                                     stderr=subprocess.STDOUT, check=False)
        result = log.read_text(encoding="utf-8", errors="replace")
        assert process.returncode == 0 and "fzero_native: PASS" in result, result[-2000:]
        assert "[bs-deluxe]" not in result, result
        return result, ram.read_bytes(), frame.read_bytes()

    title = "300-306:32,320-326:8"
    confirm_car = ",540-546:8,680-686:8"
    no_rival = ",740-746:32,780-786:32"
    ghost = no_rival + ",815-821:32"
    preview = ",850:8"
    race = ",980-985:8"
    finish = ",3350:64,3450-3456:256"

    _, active_ram, active_frame = run("roster-on", title + ",540-546:8", 610)
    _, off_ram, off_frame = run("roster-off", title + ",540-546:8", 610, FZERO_GHOSTS="0")
    assert active_ram[0x54:0x56] == off_ram[0x54:0x56] == b"\x01\x02"
    assert active_frame == off_frame, "Ghost toggle altered the stock roster art"

    grand_prix = "320-326:8,440-446:8,560-566:8,730-736:8,790-796:8,1160-1499:1"
    _, gp_on_ram, gp_on = run("grand-prix-on", grand_prix, 1500)
    _, gp_off_ram, gp_off = run("grand-prix-off", grand_prix, 1500, FZERO_GHOSTS="0")
    assert gp_on_ram[0x54:0x56] == b"\x02\x03" and gp_on_ram[0x58] == 0
    assert gp_on_ram == gp_off_ram and gp_on == gp_off, "Ghost toggle altered Grand Prix"

    _, empty_ram, empty_frame = run("empty", title + confirm_car + ghost + preview, 950)
    _, plain_ram, plain_frame = run("empty-control", title + confirm_car + no_rival + preview, 950)
    assert empty_ram[0x53:0x59] == plain_ram[0x53:0x59]
    assert empty_frame != plain_frame, "Course preview omitted the empty Ghost panel"

    for name, selection, throttle in (
        ("first", "", ",1240-1300:1"),
        ("second", "", ",1260-1315:128"),
        ("other-car", ",400-406:32", ",1240-1300:1"),
    ):
        log, ram, _ = run(name, title + selection + confirm_car + no_rival
                          + preview + race + throttle + finish, 3700,
                          FZERO_TEST_RECORDS_CUP="1")
        assert "records-fixture: finish crossing" in log and ram[0xd40] == 5, name
        assert "[practice-ghost] saved course=0" in log, name

    archive = sorted((out / "s" / "practice-ghosts").glob("*/*/run.fzg"))
    assert len(archive) == 3, archive
    streams = [path.read_bytes() for path in archive]
    assert all(stream[:4] == b"FZG\x03" for stream in streams)
    metadata = [struct.unpack_from("<5I", stream, 36) for stream in streams]
    assert [entry[0] for entry in metadata] == [1, 2, 3]
    assert metadata[0][3] == metadata[1][3] != metadata[2][3]
    assert streams[0][120:] != streams[1][120:], "Different same-car inputs stored the same poses"
    assert len((out / "s" / "save.srm").read_bytes()) == 0x800

    base = title + confirm_car + ghost + preview
    _, control_ram, control = run("race-control", title + confirm_car + no_rival + preview + race, 1312)
    capture = dict(FZERO_CAPTURE_FRAME="1300", FZERO_CAPTURE_PREFIX=str(out / "drift-stock"))
    log, stock_race, frame = run("race-1", base + race, 1312, **capture)
    assert "racing course=0 run=1 car=0" in log and stock_race[0xcf2] == 255
    assert frame != control
    for run_id, choice in ((2, ",950:2048"), (3, ",950:1024")):
        log, ram, frame = run(f"race-{run_id}", base + choice + race, 1312)
        car = metadata[run_id - 1][3]
        assert f"racing course=0 run={run_id} car={car} " in log
        assert ram[0xcf2] == control_ram[0xcf2] == 255, "A ghost occupied the native rival slot"
        assert frame != control, f"Run {run_id} was selected without a visible rival"

    # Past EOF, the native ghost holds its last pose and continues flashing.
    # Run 2 remains in view; adjacent frames must contain it exactly once.
    end_visible = []
    for frames in (1501, 1502):
        _, normal_ram, normal = run(f"end-control-{frames}", title + confirm_car + no_rival + preview + race, frames)
        _, ended_ram, ended = run(f"end-ghost-{frames}", base + ",950:2048" + race, frames)
        assert ended_ram[0xb00:0x1100] == normal_ram[0xb00:0x1100], "Ghost changed player simulation after EOF"
        end_visible.append(ended != normal)
    assert sum(end_visible) == 1, "Ghost must retain its last pose and alternate visibility after EOF"

    cosmetic, fixed_race, _ = run("animation-fix", base + race, 1312, FZERO_RULES="cgp-animation",
                                  **dict(capture, FZERO_CAPTURE_PREFIX=str(out / "drift-fixed")))
    assert "racing course=0 run=1 car=0" in cosmetic
    assert stock_race[0xb00:0x1100] == fixed_race[0xb00:0x1100]
    lower_tiles = stock.read_bytes()[0x5fe00:0x5fe40]
    vram = 224 * 1120  # Raster capture's scanlines precede its captured video RAM.
    for label, address in (("drift-stock", 0x49c1), ("drift-fixed", 0x49c0)):
        capture_data = (out / f"{label}.bin").read_bytes()
        start = vram + address * 2
        assert capture_data[start:start + len(lower_tiles)] == lower_tiles
    disabled, disabled_ram, disabled_frame = run("ghosts-disabled", base + race, 1312, FZERO_GHOSTS="0")
    assert "[practice-ghost]" not in disabled
    assert disabled_ram[0xcf2] == 255 and disabled_frame == control

    log, ram, _ = run("different-course", base + ",943:4,1150:8", 1475)
    assert ram[0x53] == 1 and "[practice-ghost] racing" not in log
    assert [path.read_bytes() for path in archive] == streams
    assert hashlib.sha256(stock.read_bytes()).hexdigest() == original

    # Snapshot carries the selected rival's identity and its playback position.
    # Rewind within the run can also reuse the exact recording prefix in memory.
    snapshot = str(out / "practice-race.state")
    rewind_log, _, rewound = run(
        "rewind", base + race, 1312, FZERO_STATE_SAVE=snapshot,
        FZERO_TEST_SAVE_FRAME="1280", FZERO_REWIND_TEST="1",
    )
    assert "actual ring restore and ten-frame resimulation identical" in rewind_log
    assert rewound == (out / "race-1.ppm").read_bytes()
    _, cold_ram, cold = run("state-load", "", 32, FZERO_STATE_LOAD=snapshot)
    _, cold_off_ram, cold_off = run("state-load-off", "", 32,
                                  FZERO_STATE_LOAD=snapshot, FZERO_GHOSTS="0")
    assert cold_ram == cold_off_ram and cold_ram[0xcf2] == 255
    assert cold != cold_off, "Cold-loading the selected ghost lost the rival"
    log, _, _ = run(
        "rewind-record", title + confirm_car + no_rival + preview + race + ",1240-1300:1" + finish,
        3700, FZERO_TEST_RECORDS_CUP="1", FZERO_TEST_SAVE_FRAME="1280",
        FZERO_REWIND_TEST="1", SNESRECOMP_SAVE_ROOT="s-rewind",
    )
    assert "actual ring restore and ten-frame resimulation identical" in log
    rewound_record = list((out / "s-rewind" / "practice-ghosts").glob("*/*/run.fzg"))
    assert len(rewound_record) == 1 and rewound_record[0].read_bytes() == streams[0]

    # A deterministic alpha.1 SRAM-format fixture. The guest program enables
    # neither recording nor playback here. The host decodes the old stream
    # and adds it to the same archive used by newly recorded runs.
    legacy_root = out / "s-legacy"
    native = legacy_root / "ghosts" / "save.srm"
    native.parent.mkdir(parents=True)
    legacy = bytearray(0x8000)
    deltas = bytes((2, 0, 0, 1, 0xff, 0xff, 0xff, 0xff))
    struct.pack_into("<5H", legacy, 0x3a0, 0, 0x4200, len(deltas), sum(deltas), 90)
    legacy[0x4200:0x4200 + len(deltas)] = deltas
    native.write_bytes(legacy)

    music_namespace = legacy_root / "rules-913a5cdb9be47066"
    music_native = music_namespace / "save.srm"
    music_ghost = music_namespace / "ghosts" / "save.srm"
    music_ghost.parent.mkdir(parents=True)
    music_progress = bytearray(0x800)
    music_progress[:5] = b"FZERO"
    for start in (0x005, 0x0ac, 0x153):
        music_progress[start:start + 165] = b"\x09\x59\x99" * 55
        if start == 0x005:
            music_progress[start:start + 3] = b"\x01\x13\x22"
        struct.pack_into("<H", music_progress, start + 165, sum(music_progress[start:start + 165]))
    music_progress[0x1fa] = 0x22
    music_native.write_bytes(music_progress)
    music_replay = bytearray(legacy)
    struct.pack_into("<H", music_replay, 0x3aa, 1)
    music_ghost.write_bytes(music_replay)

    for label in ("legacy-first", "legacy-restart"):
        log, _, _ = run(label, base + race, 1300, SNESRECOMP_SAVE_ROOT="s-legacy")
        assert log.count("imported legacy") == (2 if label == "legacy-first" else 0)
        assert "racing course=0 run=1 car=0" in log
        assert native.read_bytes() == legacy
        assert music_native.read_bytes() == music_progress and music_ghost.read_bytes() == music_replay
    log, _, _ = run("legacy-music-rival", base + ",950:2048" + race, 1300,
                   SNESRECOMP_SAVE_ROOT="s-legacy")
    assert "racing course=0 run=2 car=1" in log
    migrated_progress = (legacy_root / "save.srm").read_bytes()
    assert migrated_progress[5:8] == b"\x01\x13\x22" and migrated_progress[0x1fa] == 0x22
    legacy_archive = list((legacy_root / "practice-ghosts").glob("*/*/run.fzg"))
    assert len(legacy_archive) == 2

    (out / "summary.json").write_text(json.dumps({
        "stock_sha256": original, "passed": True,
        "metadata": metadata, "compared_visible_playback": len(archive),
        "forced_finish_crossing": True, "legacy_format_fixture": True,
        "same_session_rewind": True, "cold_state_load_ghost": True,
        "legacy_music_namespace": True,
        "grand_prix_ghost_toggle": True,
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
