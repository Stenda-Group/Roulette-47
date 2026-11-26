"""Publishes a Roulette 47 release on GitHub.

    python tools/publish.py --notes-file notes.md [--dry-run]

- the version comes from VERSION (e.g. 0.13 -> tag v0.13);
- the archive roulette47-v0.13.zip is built from build/ and ui/ with the game
  folder layout (Retail/version.dll, Retail/Roulette47/...);
- the release is created on the tag (the commit must already be pushed);
- the SHA-256 of the archive is appended to the notes;
- only the three most recent releases are kept: older releases are deleted,
  together with their tags.

The GitHub token comes from `git credential fill`; it is never printed.
Build first (tools/compiler.cmd).
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OWNER, REPO = "Stenda-Group", "Roulette-47"
KEEP = 3


def token():
    r = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                       capture_output=True, text=True, check=True)
    fields = dict(l.split("=", 1) for l in r.stdout.splitlines() if "=" in l)
    return fields["password"]


def api(method, url, body=None, data=None, content_type=None):
    if not url.startswith("http"):
        url = "https://api.github.com" + url
    if body is not None:
        data, content_type = json.dumps(body).encode(), "application/json"
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Bearer " + token())
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("User-Agent", "roulette47-publish")
    if content_type:
        req.add_header("Content-Type", content_type)
    try:
        with urllib.request.urlopen(req) as r:
            text = r.read().decode()
            return r.status, json.loads(text) if text else None
    except urllib.error.HTTPError as e:
        text = e.read().decode()
        try:
            return e.code, json.loads(text)
        except ValueError:
            return e.code, text


def build_archive(version):
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    out = dist / f"roulette47-v{version}.zip"
    files = {
        "Retail/version.dll": ROOT / "build" / "version.dll",
        "Retail/Roulette47/Roulette47.dll": ROOT / "build" / "Roulette47.dll",
        "Retail/Roulette47/LICENSE.txt": ROOT / "LICENSE",
        "Retail/Roulette47/THIRD-PARTY-NOTICES.md": ROOT / "THIRD-PARTY-NOTICES.md",
    }
    for f in sorted((ROOT / "data" / "initial_locations").glob("*.json")):
        files["Retail/Roulette47/initial_locations/" + f.name] = f
    for f in sorted((ROOT / "ui").rglob("*")):
        if f.is_file():
            files["Retail/Roulette47/ui/" + f.relative_to(ROOT / "ui").as_posix()] = f
    for name, src in files.items():
        if not src.exists():
            sys.exit(f"missing {src}: build first (tools/compiler.cmd)")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, src in files.items():
            z.write(src, name)
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--notes-file", required=True)
    p.add_argument("--dry-run", action="store_true")
    a = p.parse_args()

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    tag = "v" + version
    notes = pathlib.Path(a.notes_file).read_text(encoding="utf-8")
    archive = build_archive(version)
    sha = hashlib.sha256(archive.read_bytes()).hexdigest()
    print(f"archive {archive.name} ({archive.stat().st_size // 1024} KB), SHA-256 {sha}")
    # The checksum goes at the end of the notes, so anyone can check the download.
    notes = notes.rstrip() + f"\n\n## Checksum\n\n`{archive.name}` — SHA-256 `{sha}`\n"
    if a.dry_run:
        return

    code, existing = api("GET", f"/repos/{OWNER}/{REPO}/releases/tags/{tag}")
    if code == 200:
        sys.exit(f"release {tag} already exists: bump VERSION")

    code, rel = api("POST", f"/repos/{OWNER}/{REPO}/releases",
                    {"tag_name": tag, "target_commitish": "main", "name": f"Roulette 47 {tag}", "body": notes,
                     "draft": False, "prerelease": False})
    if code != 201:
        sys.exit(f"release not created ({code}): {rel}")
    upload = rel["upload_url"].split("{")[0] + "?name=" + archive.name
    code, asset = api("POST", upload, data=archive.read_bytes(), content_type="application/zip")
    if code != 201:
        sys.exit(f"archive not uploaded ({code}): {asset}")
    print(f"published {rel['html_url']}")

    # Keep the three most recent releases.
    code, releases = api("GET", f"/repos/{OWNER}/{REPO}/releases?per_page=100")
    releases = sorted(releases, key=lambda r: r["created_at"], reverse=True)
    for old in releases[KEEP:]:
        api("DELETE", f"/repos/{OWNER}/{REPO}/releases/{old['id']}")
        api("DELETE", f"/repos/{OWNER}/{REPO}/git/refs/tags/{old['tag_name']}")
        print(f"deleted old release {old['tag_name']}")


if __name__ == "__main__":
    main()
