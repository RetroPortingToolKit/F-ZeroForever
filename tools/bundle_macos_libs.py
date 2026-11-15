"""Make a staged macOS release self-contained: bundle and re-link its dylibs.

  bundle_macos_libs.py STAGE --build BUILD --notices DIR

Walks `otool -L` from every Mach-O executable at the top of STAGE, copies each
non-system dependency into STAGE/lib, rewrites the references to
@executable_path/lib (executables) or @loader_path (libraries), copies each
library's licence into --notices, then ad-hoc signs everything, because Apple
silicon refuses to run modified binaries without a valid signature.

PyInstaller output (FZeroConvertContent) is self-contained and signed by
PyInstaller; rewriting it would corrupt its appended archive, so it is skipped.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

SYSTEM_PREFIXES = ("/usr/lib/", "/System/")
SKIP = {"FZeroConvertContent"}


def is_macho(path):
    with path.open("rb") as handle:
        magic = handle.read(4)
    return magic in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def dependencies(binary):
    output = subprocess.check_output(["otool", "-L", str(binary)], text=True).splitlines()[1:]
    return [line.strip().split(" (compatibility")[0] for line in output if line.strip()]


def rpaths(binary):
    output = subprocess.check_output(["otool", "-l", str(binary)], text=True)
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset", output)


def resolve(reference, binary, build):
    """Resolve @rpath/@loader_path/@executable_path references to a real file."""
    if reference.startswith("@rpath/"):
        name = reference[len("@rpath/"):]
        for rpath in rpaths(binary):
            base = Path(rpath.replace("@loader_path", str(binary.parent))
                        .replace("@executable_path", str(binary.parent)))
            if (base / name).is_file():
                return (base / name).resolve()
        hits = sorted(Path(build).rglob(Path(name).name))
        if hits:
            return hits[0].resolve()
        raise SystemExit(f"Cannot resolve {reference} from {binary}")
    for token in ("@loader_path", "@executable_path"):
        if reference.startswith(token):
            return Path(reference.replace(token, str(binary.parent), 1)).resolve()
    return Path(reference).resolve()


def licence_for(library):
    """Find the licence shipped with a Homebrew keg or a fetched source tree."""
    for parent in library.parents:
        candidates = [p for p in parent.glob("*") if p.is_file() and
                      re.match(r"(LICEN[CS]E|COPYING|COPYRIGHT)", p.name, re.IGNORECASE)]
        if candidates:
            return sorted(candidates)[0]
        if parent.name == "Cellar" or parent == parent.parent:
            break
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--notices", type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve()
    lib_dir = stage / "lib"
    lib_dir.mkdir(exist_ok=True)
    args.notices.mkdir(parents=True, exist_ok=True)
    executables = [p for p in stage.iterdir()
                   if p.is_file() and p.name not in SKIP and p.suffix == "" and is_macho(p)]
    if not any(p.name == "F-Zero Forever" for p in executables):
        raise SystemExit("Staged game executable is missing")
    bundled = {}  # real source path -> bundled name
    pending = [(p, True) for p in executables]
    while pending:
        binary, is_executable = pending.pop()
        for reference in dependencies(binary):
            if reference.startswith(SYSTEM_PREFIXES):
                continue
            if Path(reference).name == binary.name and not is_executable:
                continue  # the library's own install name
            source = resolve(reference, binary, args.build)
            if not source.is_file():
                raise SystemExit(f"Missing dependency {source} of {binary}")
            name = source.name
            if source not in bundled:
                if name in bundled.values():
                    raise SystemExit(f"Two different libraries named {name}")
                bundled[source] = name
                target = lib_dir / name
                shutil.copy2(source, target)
                target.chmod(0o755)
                subprocess.run(["install_name_tool", "-id", f"@rpath/{name}", str(target)], check=True)
                pending.append((target, False))
                notice = licence_for(source)
                if notice is None:
                    build_notices = sorted(args.build.rglob("_deps/*-src/LICENSE.txt"))
                    notice = next((n for n in build_notices if name.lower().startswith(
                        n.parent.name.split("-src")[0].lower())), None)
                if notice is None:
                    raise SystemExit(f"No licence found for bundled library {source}")
                shutil.copy2(notice, args.notices / (Path(name).stem.split(".")[0] + ".txt"))
            new = (f"@executable_path/lib/{name}" if is_executable else f"@loader_path/{name}")
            if reference != new:
                subprocess.run(["install_name_tool", "-change", reference, new, str(binary)], check=True)
    for binary in [*lib_dir.iterdir(), *executables]:
        subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)
    for binary in executables:
        unresolved = [d for d in dependencies(binary) if not d.startswith(SYSTEM_PREFIXES)
                      and not d.startswith("@executable_path/lib/")]
        if unresolved:
            raise SystemExit(f"{binary.name} still references {unresolved}")
    print(f"Bundled {len(bundled)} libraries for {len(executables)} executables")


if __name__ == "__main__":
    main()
