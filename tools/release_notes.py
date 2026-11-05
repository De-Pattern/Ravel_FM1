#!/usr/bin/env python3
"""A release's notes: what changed (its section of CHANGELOG.md, or its commits when there is none), then
the files and how to install them.

  release_notes.py vX.Y.Z REPO_URL PAGES_URL > notes.md
  release_notes.py --versions                 (the versions CHANGELOG.md has sections for)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def sections():
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8") if (ROOT / "CHANGELOG.md").exists() else ""
    out, cur = {}, None
    for line in text.splitlines():
        m = re.match(r"^## +v?(\d+\.\d+\.\d+\S*)\s*$", line)
        if m:
            cur = m.group(1)
            out[cur] = []
        elif line.startswith("## "):
            cur = None
        elif cur:
            if re.match(r"^(New|Fixed|Faster|Removed|Changed)$", line.strip()):
                line = f"**{line.strip()}**"     # (the kinds of change, as small headings)
            out[cur].append(line)
    return {k: "\n".join(v).strip() for k, v in out.items()}


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def commits(tag):
    """the commits since the tag before this one (their subjects; release bookkeeping left out)"""
    tags = [t for t in git("tag", "--list", "v*", "--sort=-v:refname").splitlines() if t]
    prev = None
    if tag in tags:
        i = tags.index(tag)
        same_line = [t for t in tags[i + 1:] if t.startswith("v0.") == tag.startswith("v0.")]
        prev = same_line[0] if same_line else None
    rng = f"{prev}..{tag}" if prev else tag
    subj = git("log", "--format=%s", "--no-merges", rng).splitlines()
    skip = re.compile(r"^(Release workflow|Releases from|README|Site:|CI\b|Merge )", re.I)
    subj = [s for s in subj if s and not skip.match(s)]
    return "\n".join(f"- {s}" for s in subj[:25])


def notes(tag, repo, pages):
    ver = tag[1:] if tag.startswith("v") else tag
    body = sections().get(ver)
    if not body:
        c = commits(tag)
        body = f"Changes\n{c}" if c else ""
    test = ver.startswith("0.")
    head = f"**Ravel {ver}**{' (test build)' if test else ''} for the M-VAVE FM-1."
    files = f"""### Files

- `Ravel-FM1.fwsc`: the FM-1 firmware. Install it from the web installer ({pages}install/, Chrome or Edge, over USB) or with `python3 tools/fm1_install.py Ravel-FM1.fwsc`. Keep the FM-1 connected for the whole update. The installer page can also put M-VAVE's own firmware back.
- `ravel-emulator.html`: the emulator in one file. Open it in a browser; no install.
- `ravel-manual.pdf`: the manual.
- `SHA256SUMS.txt`: checksums.

Live emulator: {pages} · Installer: {pages}install/ · Manual: {pages}manual.html · All changes: {repo}/blob/main/CHANGELOG.md"""
    parts = [head]
    if body:
        parts.append("### What's new\n\n" + body)
    parts.append(files)
    return "\n\n".join(parts) + "\n"


if __name__ == "__main__":
    if sys.argv[1:2] == ["--versions"]:
        print("\n".join(sections().keys()))
    else:
        sys.stdout.write(notes(sys.argv[1], sys.argv[2].rstrip("/"), sys.argv[3]))
