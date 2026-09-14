#!/usr/bin/env python3
"""Build and validate a local, non-publishing Quarry release bundle."""
from __future__ import annotations
import argparse, gzip, hashlib, io, re, shutil, subprocess, sys, tarfile, tempfile, zipfile
from pathlib import Path

TAG_RE = re.compile(r"^v(?P<version>\d+\.\d+\.\d+)(?:-rc\.\d+)?$")

def run(command, cwd=None):
    return subprocess.check_output(command, cwd=cwd, text=True).strip()

def archive_tree(root, output, kind):
    prefix = root.name + "/"
    entries = sorted(path for path in root.rglob("*") if path.is_file())
    if kind == "tar":
        raw = io.BytesIO()
        with tarfile.open(fileobj=raw, mode="w:", format=tarfile.PAX_FORMAT) as archive:
            for path in entries:
                info = tarfile.TarInfo(prefix + path.relative_to(root).as_posix())
                data = path.read_bytes()
                info.size = len(data); info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644; info.mtime = 0
                archive.addfile(info, io.BytesIO(data))
        output.write_bytes(gzip.compress(raw.getvalue(), mtime=0))
    else:
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in entries:
                info = zipfile.ZipInfo(prefix + path.relative_to(root).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o755 if path.stat().st_mode & 0o111 else 0o644) << 16
                archive.writestr(info, path.read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release-notes", type=Path, required=True)
    args = parser.parse_args()
    match = TAG_RE.fullmatch(args.tag)
    if not match: parser.error("--tag must match vX.Y.Z or vX.Y.Z-rc.N")
    version = match.group("version"); repo = Path(__file__).resolve().parents[1]
    args.output = args.output.resolve()
    if run(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=no"]):
        raise SystemExit("release artifacts require a clean tracked working tree")
    major_minor = (repo / "git_version").read_text(encoding="utf-8").strip()
    if ".".join(version.split(".")[:2]) != major_minor: raise SystemExit("release version does not match git_version")
    notes = args.release_notes.resolve()
    if not notes.is_file() or notes.name != f"release-notes-{args.tag}.md": raise SystemExit("release notes name does not match tag")
    if args.output.exists() and any(args.output.iterdir()): raise SystemExit("staging directory is not empty")
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="quarry-release-") as temporary:
        tree = Path(temporary) / f"quarry-{args.tag}"; tree.mkdir()
        data = subprocess.run(["git", "-C", str(repo), "archive", "HEAD"], check=True, stdout=subprocess.PIPE).stdout
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as source: source.extractall(tree)
        fallback = tree / "cmake" / "QuarryResolvedVersion.cmake"
        sha = run(["git", "-C", str(repo), "rev-parse", "HEAD"])
        fallback.write_text(f'# Generated release fallback; do not edit.\nset(QUARRY_VERSION "{version}")\nset(QUARRY_ARCHIVE_TAG "{args.tag}")\nset(QUARRY_GIT_SHA "{sha}")\n', encoding="utf-8")
        archive_tag = args.tag[1:] if args.tag.startswith("v") else args.tag
        archive_tree(tree, args.output / f"quarry-{archive_tag}.tar.gz", "tar")
        archive_tree(tree, args.output / f"quarry-{archive_tag}.zip", "zip")
        python_dir = tree / "runtime" / "python"
        subprocess.run([sys.executable, "-m", "build", "--wheel", "--sdist", "--outdir", str(args.output)], cwd=python_dir, check=True)
    shutil.copy2(notes, args.output / notes.name)
    subprocess.run([sys.executable, str(repo / "tools" / "validate_release_artifacts.py"), "--root", str(args.output), "--version", version, "--tag", args.tag], check=True)
    files = sorted(path for path in args.output.iterdir() if path.is_file())
    (args.output / "SHA256SUMS").write_text("\n".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in files) + "\n", encoding="utf-8")
    print(f"release artifact validation passed: {version} ({args.tag})")
    return 0

if __name__ == "__main__": raise SystemExit(main())
