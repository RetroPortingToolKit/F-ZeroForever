"""Verify retail HLE music and ghost playback using synthetic PCM."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "stock", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    build, stock, out = (path.resolve() for path in (args.build, args.stock, args.output))
    out.mkdir(parents=True, exist_ok=False)
    exe = build / ("FZeroSNESRecompHeadless.exe" if os.name == "nt"
                   else "FZeroSNESRecompHeadless")
    original = hashlib.sha256(stock.read_bytes()).hexdigest()

    pack = out / "synthetic pack"
    pack.mkdir()
    for song in range(1, 25):
        (pack / f"test-{song}.pcm").write_bytes(
            b"MSU1" + bytes(4) + struct.pack("<hh", song * 500, -song * 500) * 4410
        )
    # A legacy numbering marker may coexist with PCM files. Its bytes are ignored.
    (pack / "f-zero_msu1.ips").write_bytes(b"numbered soundtrack marker")
    missing = out / "empty soundtrack"
    missing.mkdir()
    (missing / "test-10.pcm").write_bytes(b"MSU1" + bytes(4))

    clean = {key: value for key, value in os.environ.items()
             if not key.startswith(("FZERO_", "SNESRECOMP_", "SDL_", "LNG_"))}
    clean.update(SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy",
                 FZERO_PACK_LOADER="0", FZERO_GHOSTS="1",
                 SNESRECOMP_INPUT_SCRIPT="300-306:32,320-326:8,540-546:8,680-686:8,"
                                         "740-746:32,780-786:32,850:8,980-985:8")
    results = {}
    observations = {}
    for name, music in (("retail", "off"), ("numbered", f'"{pack / "test"}"'),
                        ("catalog", str(pack / "test")),
                        ("missing", str(missing / "test"))):
        case = out / name
        case.mkdir()
        audio_path = case / "audio.wav"
        ram_path = case / "ram.bin"
        apu_path = case / "apu.bin"
        env = dict(clean, SNESRECOMP_MSU1=music, SNESRECOMP_SAVE_ROOT="s",
                   SNESRECOMP_WAV=str(audio_path), SNESRECOMP_WRAM_DUMP=str(ram_path),
                   FZERO_TEST_APURAM_DUMP=str(apu_path))
        if name == "catalog":
            env["FZERO_RULES"] = "cgp-msu"
        with (case / "run.log").open("wb") as log:
            result = subprocess.run([str(exe), str(stock), "1400"], cwd=case,
                                    env=env, stdout=log, stderr=subprocess.STDOUT, check=False)
        log = (case / "run.log").read_text(encoding="utf-8", errors="replace")
        assert result.returncode == 0 and "fzero_native: PASS" in log, (name, log[-2000:])
        assert "[bs-deluxe]" not in log
        ram = ram_path.read_bytes()
        assert ram[0x54:0x56] == b"\x02\x03", (name, ram[0x54:0x56].hex())
        with wave.open(str(audio_path), "rb") as wav:
            audio = wav.readframes(wav.getnframes())
        pairs = list(struct.iter_unpack("<hh", audio))
        title_samples = sum(pair == (2000, -2000) for pair in pairs)
        # Native engine effects are centered, while the test signal is stereo.
        race_samples = sum(left - right == 10000 for left, right in pairs[-100000:])
        results[name] = {"title_pcm_samples": title_samples, "race_pcm_samples": race_samples}
        observations[name] = (ram, apu_path.read_bytes(), audio)
        if name == "numbered":
            assert any("command=4 " in line and " track=4 output=PCM" in line
                       for line in log.splitlines()), log[-2000:]
            assert title_samples > 1000, results[name]
        if name in ("numbered", "catalog"):
            assert "command=6 course=0 track=10 output=PCM" in log, log[-2000:]
            assert race_samples > 1000, results[name]
            assert observations["retail"][:2] == observations[name][:2], "PCM altered simulation"
        else:
            assert "output=PCM" not in log and any(audio)
    assert observations["retail"] == observations["missing"], "Missing track altered native audio"
    assert hashlib.sha256(stock.read_bytes()).hexdigest() == original
    (out / "result.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
