"""Fetch native dependency sources, checking the committed lockfile."""
from pathlib import Path
import hashlib, json, tarfile, urllib.request

ROOT = Path(__file__).resolve().parents[1]

def main():
    lock = json.loads((ROOT / 'sources.json').read_text())
    for name, spec in lock.items():
        dest = ROOT / 'external' / name
        if (dest / '.verified').exists():
            if (dest / '.verified').read_text() == spec['sha256']:
                continue
            raise RuntimeError(f'{name}: unexpected existing source')
        archive = ROOT / 'external' / (name + '.tar.gz')
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.exists():
            with urllib.request.urlopen(spec['url']) as src, archive.open('wb') as out:
                import shutil
                shutil.copyfileobj(src, out)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != spec['sha256']:
            raise RuntimeError(f'{name}: SHA256 mismatch')
        dest.mkdir(exist_ok=True)
        with tarfile.open(archive) as tf:
            members = tf.getmembers()
            for entry in members:
                # Unused Ant project aliases in the official SDL archive.
                # Do not create filesystem links while unpacking sources.
                if entry.issym() and entry.name.startswith('SDL2-2.32.8/android-project-ant/'):
                    continue
                parts = Path(entry.name).parts[1:]
                if not parts:
                    continue
                entry.name = '/'.join(parts)
                if not (entry.isdir() or entry.isfile()) or not (dest / entry.name).resolve().is_relative_to(dest.resolve()):
                    raise RuntimeError(f'{name}: unsafe archive member')
                tf.extract(entry, dest, filter='data')
        (dest / '.verified').write_text(spec['sha256'])
        print(name, 'verified', flush=True)

if __name__ == '__main__':
    main()
