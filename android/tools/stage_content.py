"""Reuse a verified without-MSU player archive's content, excluding binaries."""
import argparse, zipfile
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'app/src/main/assets/content.zip');a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    count=0
    with zipfile.ZipFile(a.archive) as source, zipfile.ZipFile(a.output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as dest:
        for entry in source.infolist():
            if entry.is_dir():continue
            parts=entry.filename.split('/')
            if len(parts)>1 and parts[0] not in ('mods','assets','licenses'):parts=parts[1:]
            name='/'.join(parts)
            if not parts or parts[0] not in ('mods','assets','licenses'):continue
            if '..' in parts or name.startswith('/') or '\\' in name:raise ValueError(name)
            if name.startswith('assets/shaders/') or name.endswith(('.exe','.dll','.cmd','.ps1','.sh')):continue
            if 'cd-speed' in name or 'fast-loading' in name:raise ValueError('forbidden release mod: '+name)
            dest.writestr(name,source.read(entry));count+=1
    print(f'{count} content files staged in {a.output}')

if __name__=='__main__':main()
