"""Private-ROM integration: the standalone Practice ghosts switch.

Ghosts are native BS F-Zero Deluxe code. This drives the real menus with pad
input and checks three things:

* ghosts alone select the Deluxe program for otherwise stock content, where a
  Practice run records, saves through the native Save Ghost dialog, and plays
  back after a reboot;
* with ghosts off the same program offers no Ghost rival, records nothing and
  never raises Save Ghost, with stock BS cars and with the CGP roster;
* the stock cartridge and its ghost twin share records in both directions,
  each writing only its own files, including under a rule namespace.

The finish fixture seeds the last lap rather than driving a full race. Raw
frames and saves stay in --out.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# Title -> Practice -> car page -> confirm -> rival list (cursor on the first car).
MENU = "300-306:32,320-326:8,600-606:8,760-766:8"
DOWN, UP, SELECT, START, A, LEFT = 32, 16, 4, 8, 256, 64
RACE = ",1200-1206:8,1300-1306:8,1700-1706:8"
SAVE_GHOST = ",4100-4106:256,4600-4606:64,4700-4706:256"  # Results, YES, confirm.


def press(frame, button):
    return f",{frame}-{frame + 3}:{button}"


def league(save, cup):
    start = (5, 0xAC, 0x153)[cup]
    block = save[start:start + 167]
    assert sum(block[:165]) & 0xFFFF == int.from_bytes(block[165:], "little"), "league checksum"
    return block


def ghost_row_colours(ppm):
    """Distinct colours where the rival panel prints GHOST (4:3 native frame)."""
    data = ppm.read_bytes()
    assert data.startswith(b"P6"), ppm
    fields, offset = [], 2
    while len(fields) < 3:
        while data[offset:offset + 1].isspace():
            offset += 1
        end = offset
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(int(data[offset:end]))
        offset = end
    width, height = fields[0], fields[1]
    pixels = data[offset + 1:]
    assert width == 256 and height == 224, (width, height)
    return len({pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]
                for y in range(164, 176) for x in range(136, 232)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "stock", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    a = parser.parse_args()
    build, stock, out = a.build.resolve(), a.stock.resolve(), a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    original = stock.read_bytes()
    clean = {k: v for k, v in os.environ.items()
             if not k.startswith(("FZERO_", "SNESRECOMP_", "SDL_", "LNG_"))}
    exe = build / ("FZeroSNESRecompHeadless" + (".exe" if os.name == "nt" else ""))

    def run(folder, label, frames, inputs, **settings):
        folder = out / folder
        if not folder.exists():
            folder.mkdir()
            shutil.copytree(ROOT / "assets", folder / "assets")
        env = dict(clean, SNESRECOMP_SAVE_ROOT="s", SNESRECOMP_INPUT_SCRIPT=inputs,
                   SNESRECOMP_WRAM_DUMP=str(folder / (label + ".ram")),
                   SNESRECOMP_FRAME_DUMP=str(folder / (label + ".ppm")),
                   FZERO_TEST_SRAM_DUMP=str(folder / (label + ".sram")),
                   FZERO_TEST_SAVE_SRAM="1", **settings)
        proc = subprocess.run([str(exe), str(stock), str(frames)], cwd=folder, env=env,
                              capture_output=True, text=True, timeout=300)
        log = proc.stdout + proc.stderr
        (folder / (label + ".log")).write_text(log, encoding="utf-8")
        assert proc.returncode == 0, (label, log[-2000:])
        return ((folder / (label + ".ram")).read_bytes(),
                (folder / (label + ".sram")).read_bytes(), log, folder)

    report = {}
    no_rival = MENU + press(900, DOWN) + press(950, DOWN)
    ghost_rival = no_rival + press(1000, DOWN)

    # Captures without the switch keep their previous program.
    ram, sram, log, _ = run("default", "boot", 400, "")
    assert len(sram) == 0x800 and "[bs-deluxe]" not in log, "plain captures left the stock cartridge"
    ram, sram, log, _ = run("default-off", "boot", 400, "", FZERO_GHOSTS="0")
    assert len(sram) == 0x800 and "[bs-deluxe]" not in log, "ghosts off selected Deluxe"

    # Ghosts alone: stock content on the Deluxe program.
    ram, sram, log, folder = run("only", "menu", 1040, ghost_rival, FZERO_GHOSTS="1")
    assert len(sram) == 0x8000 and "[ghosts] saves: s/ghosts/save.srm" in log, "ghost-only save namespace"
    assert ram[0x54:0x57] == b"\1\1\3" and ram[0x58] and ram[0x14C8A] == 0xFE, "Ghost rival unreachable"
    assert ghost_row_colours(folder / "menu.ppm") > 1, "Ghost row not shown"
    ram, _, _, _ = run("only", "cars", 1100, ghost_rival + press(1050, SELECT), FZERO_GHOSTS="1")
    assert ram[0x14C8A] == 0 and ram[0x52] < 4, "Select did not wrap through the four stock cars"
    # Grand Prix offers the three stock leagues only; three Downs wrap to Knight.
    ram, _, log, _ = run("only", "grand-prix", 1500,
                         "320-326:8,440-446:8,560-566:8" + press(660, DOWN) + press(680, DOWN) +
                         press(700, DOWN) + ",730-736:8,790-796:8,1160-1499:1", FZERO_GHOSTS="1")
    assert "[track-library] menu 3/3: King League" in log, "league list is not the stock three"
    assert ram[0x54:0x56] == b"\2\3" and not ram[0x58] and ram[0x90] == 0, ("Grand Prix league", ram[0x90])
    ram, saved, _, folder = run("only", "record", 5000, no_rival + RACE + SAVE_GHOST,
                                FZERO_GHOSTS="1", FZERO_TEST_RECORDS_CUP="1")
    assert saved[0x3A0] == 0 and saved[0x3AA] < 4, ("ghost not saved", saved[0x3A0:0x3B0].hex())
    start = int.from_bytes(saved[0x3A2:0x3A4], "little")
    size = int.from_bytes(saved[0x3A4:0x3A6], "little")
    assert size and sum(saved[start:start + size]) & 65535 == int.from_bytes(saved[0x3A6:0x3A8], "little")
    assert (folder / "s/ghosts/save.srm").read_bytes() == saved
    assert not (folder / "s/bs-deluxe").exists() and not (folder / "s/save.srm").exists()
    ram, replayed, _, _ = run("only", "playback", 2100, ghost_rival + RACE, FZERO_GHOSTS="1")
    assert ram[0x54:0x56] == b"\2\3" and ram[0x14CA0] & 0x80, "saved ghost did not play after reboot"
    assert replayed[0x3A0:0x3B0] == saved[0x3A0:0x3B0], "playback changed the saved ghost"
    report["ghost_only"] = dict(stream_bytes=size, save_root="s/ghosts", playback=True)

    # Ghosts off on the Deluxe program: stock BS cars, then the CGP roster.
    for name, settings, to_no_rival in (
            ("off-bs", dict(FZERO_DELUXE_DATA="embedded"), no_rival),
            ("off-cgp", dict(FZERO_DELUXE_DATA="embedded", FZERO_BS_CARS="0", FZERO_BS_TRACKS="0",
                             FZERO_CGP_CARS="7", FZERO_CGP_REBALANCE="15"),
             MENU + press(900, UP))):
        settings["FZERO_GHOSTS"] = "0"
        ram, sram, log, folder = run(name, "menu", 1040, to_no_rival + press(1000, DOWN), **settings)
        assert len(sram) == 0x8000 and ram[0x54:0x57] == b"\1\1\3" and ram[0x58], (name, "not the rival list")
        assert ram[0x14C8A] == 0xFF, (name, "Down reached Ghost", hex(ram[0x14C8A]))
        assert ghost_row_colours(folder / "menu.ppm") == 1, (name, "Ghost row still drawn")
        ram, _, _, _ = run(name, "wrap", 1100, to_no_rival + press(1000, DOWN) + press(1050, SELECT), **settings)
        assert ram[0x14C8A] == 0, (name, "Select did not wrap to the first car", hex(ram[0x14C8A]))
        ram, sram, _, _ = run(name, "finish", 5000, to_no_rival + RACE + SAVE_GHOST,
                              FZERO_TEST_RECORDS_CUP="1", **settings)
        assert ram[0x54] == 3 and not ram[0x14CA0] & 0x84, (name, "ghost state", hex(ram[0x14CA0]))
        assert ram[0x56] > 3, (name, "results did not pass the Save Ghost step", ram[0x54:0x57].hex())
        assert sram[0x3A0] == 0xFF, (name, "a ghost was written", sram[0x3A0:0x3B0].hex())
        report[name] = dict(ghost_row_hidden=True, save_ghost_prompt=False)
    # The same content with ghosts on still records.
    ram, sram, _, _ = run("on-bs", "finish", 5000, no_rival + RACE + SAVE_GHOST, FZERO_DELUXE_DATA="embedded",
                          FZERO_TEST_RECORDS_CUP="1")
    assert sram[0x3A0] == 0 and ram[0x54:0x57] == b"\3\3\3", "Deluxe content lost Save Ghost"

    # Twin records: stock cartridge first, then its ghost twin, then back.
    for name, rules in (("twin", ""), ("twin-rules", "cgp-dash,cgp-spin")):
        settings = dict(FZERO_RULES=rules, FZERO_TEST_RECORDS_CUP="1")
        _, stock_save, log, folder = run(name, "stock", 5000, no_rival + RACE + SAVE_GHOST,
                                         FZERO_GHOSTS="0", **settings)
        roots = [p.parent for p in (folder / "s").rglob("save.srm")]
        assert len(roots) == 1 and len(stock_save) == 0x800, (name, roots)
        base = roots[0]
        assert (base == folder / "s") == (not rules), (name, "rule namespace", base)
        first = league(stock_save, 0)[:3]
        assert first[0] & 15 < 9, (name, "no stock record")
        _, twin_save, log, _ = run(name, "ghosts", 5000,
                                   no_rival + ",1200-1206:8,1300-1306:8" + press(1500, DOWN) +
                                   ",1700-1706:8" + SAVE_GHOST, FZERO_GHOSTS="1", **settings)
        assert (base / "ghosts/save.srm").read_bytes() == twin_save, (name, "twin is not beside the stock save")
        assert "[records] merged times from " + base.relative_to(folder).as_posix() in log, (name, "no merge")
        assert league(twin_save, 0)[:3] == first, (name, "stock record missing on the Deluxe program")
        second = league(twin_save, 0)[33:36]
        assert second[0] & 15 < 9 and twin_save[0x3A0] == 1, (name, "ghost-side record or ghost missing")
        assert (base / "save.srm").read_bytes() == stock_save, (name, "the ghost twin wrote the stock save")
        ghost_file = (base / "ghosts/save.srm").read_bytes()
        _, merged, log, _ = run(name, "stock-again", 400, "", FZERO_GHOSTS="0", FZERO_RULES=rules)
        assert league(merged, 0)[:3] == first and league(merged, 0)[33:36] == second, (name, "not merged back")
        assert (base / "ghosts/save.srm").read_bytes() == ghost_file, (name, "the stock side wrote its twin")
        assert len((base / "save.srm").read_bytes()) == 0x800
        report[name] = dict(namespace=base.relative_to(folder / "s").as_posix() or ".", both_directions=True)

    assert stock.read_bytes() == original
    (out / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Practice ghosts switch PASS", json.dumps(report))


if __name__ == "__main__":
    main()
