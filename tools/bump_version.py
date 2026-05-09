"""Bump version across all hi_xxx module headers and Projucer project files.

Usage:
  python tools/bump_version.py bump NEW_VERSION       # rewrite version files only
  python tools/bump_version.py resave                 # run Projucer --resave on both projects
  python tools/bump_version.py finalize NEW_VERSION   # stage + commit + tag

Example: python tools/bump_version.py bump 4.2.0
"""
import platform
import re
import subprocess
import sys
from pathlib import Path


VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def update_module_header(path: Path, new_version: str) -> bool:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"(BEGIN_JUCE_MODULE_DECLARATION.*?version:\s+)(\d+\.\d+\.\d+)",
        re.DOTALL,
    )
    new_text, n = pattern.subn(rf"\g<1>{new_version}", text, count=1)
    if n == 0:
        return False
    if new_text == text:
        return False
    path.write_text(new_text, encoding="utf-8")
    return True


def update_jucer(path: Path, new_version: str) -> bool:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(r'(<JUCERPROJECT\b[^>]*?\bversion=")(\d+\.\d+\.\d+)(")', re.DOTALL)
    new_text, n = pattern.subn(rf"\g<1>{new_version}\g<3>", text, count=1)
    if n == 0:
        return False
    if new_text == text:
        return False
    path.write_text(new_text, encoding="utf-8")
    return True


def collect_targets(root: Path) -> list[Path]:
    targets: list[Path] = []
    for header in sorted(root.glob("hi_*/hi_*.h")):
        if header.stem == header.parent.name:
            targets.append(header)
    targets.append(root / "projects" / "plugin" / "HISE.jucer")
    targets.append(root / "projects" / "standalone" / "HISE Standalone.jucer")
    targets.append(root / "currentGitHash.txt")
    targets.append(root / "hi_backend" / "backend" / "currentGit.h")
    targets.append(root / "projects" / "plugin" / "JuceLibraryCode" / "AppConfig.h")
    targets.append(root / "projects" / "standalone" / "JuceLibraryCode" / "AppConfig.h")
    return targets


def cmd_bump(root: Path, new_version: str) -> int:
    changed = []
    for header in sorted(root.glob("hi_*/hi_*.h")):
        if header.stem != header.parent.name:
            continue
        if update_module_header(header, new_version):
            changed.append(header)

    for jucer in [
        root / "projects" / "plugin" / "HISE.jucer",
        root / "projects" / "standalone" / "HISE Standalone.jucer",
    ]:
        if not jucer.exists():
            print(f"warning: missing {jucer}", file=sys.stderr)
            continue
        if update_jucer(jucer, new_version):
            changed.append(jucer)

    print(f"bumped {len(changed)} file(s) to {new_version}:")
    for p in changed:
        print(f"  {p.relative_to(root)}")
    return 0


def cmd_resave(root: Path) -> int:
    if platform.system() == "Windows":
        projucer = root / "JUCE" / "projucer" / "Projucer.exe"
    else:
        projucer = root / "JUCE" / "projucer" / "Projucer"
    if not projucer.exists():
        print(f"error: Projucer not found at {projucer}", file=sys.stderr)
        return 1

    jucer_files = [
        root / "projects" / "plugin" / "HISE.jucer",
        root / "projects" / "standalone" / "HISE Standalone.jucer",
    ]
    for jucer in jucer_files:
        print(f"resaving {jucer.relative_to(root)}")
        subprocess.run([str(projucer), "--resave", str(jucer)], cwd=root, check=True)
    return 0


def cmd_finalize(root: Path, new_version: str) -> int:
    targets = [p for p in collect_targets(root) if p.exists()]
    subprocess.run(
        ["git", "add", "-f", "--"] + [str(p) for p in targets],
        cwd=root,
        check=True,
    )

    diff = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=root)
    if diff.returncode == 0:
        print("nothing to commit (files already at target version)")
    else:
        subprocess.run(
            ["git", "commit", "-m", f"bump version to {new_version}"],
            cwd=root,
            check=True,
        )

    tag = f"v{new_version}"
    existing = subprocess.run(
        ["git", "tag", "--list", tag],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    )
    if existing.stdout.strip() == tag:
        print(f"tag {tag} already exists, skipping")
    else:
        subprocess.run(["git", "tag", tag], cwd=root, check=True)
        print(f"created tag {tag}")
    return 0


def main() -> int:
    root = repo_root()
    if len(sys.argv) >= 2 and sys.argv[1] == "resave" and len(sys.argv) == 2:
        return cmd_resave(root)
    if len(sys.argv) != 3 or sys.argv[1] not in ("bump", "finalize"):
        print(__doc__, file=sys.stderr)
        return 2
    subcmd = sys.argv[1]
    new_version = sys.argv[2].strip().lstrip("v")
    if not VERSION_RE.match(new_version):
        print(f"error: '{new_version}' is not a X.Y.Z version", file=sys.stderr)
        return 2

    if subcmd == "bump":
        return cmd_bump(root, new_version)
    return cmd_finalize(root, new_version)


if __name__ == "__main__":
    sys.exit(main())
