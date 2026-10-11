"""Validate locked dependencies; release mode requires committed qualification evidence."""
import argparse
import hashlib
import json
import math
import pathlib
import re
import subprocess
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEMVER = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-rc\.([1-9][0-9]*))?\Z")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def committed_json(relative):
    path = pathlib.PurePosixPath(relative)
    require(not path.is_absolute() and ".." not in path.parts, "Evidence must be a repository-relative path")
    subprocess.run(["git", "ls-files", "--error-unmatch", relative], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return json.loads((ROOT / relative).read_text(encoding="utf-8-sig"))


def check_manifest(manifest):
    require(manifest["schemaVersion"] == 1, "Unsupported manifest schema")
    require(SEMVER.fullmatch(manifest["appVersion"]), "App version must be stable or rc.N SemVer")
    parts = manifest["packageVersion"].split(".")
    require(len(parts) == 3 and all(re.fullmatch(r"0|[1-9][0-9]*", part) for part in parts), "MSI version must contain three integers")
    require(int(parts[0]) <= 255 and int(parts[1]) <= 255 and int(parts[2]) <= 65535, "MSI version exceeds Windows Installer limits")
    require(manifest["releaseRepository"] == "DevLn737/jellyfin-desktop", "Unexpected release repository")
    require(manifest["windows"]["architecture"] == "x64" and manifest["windows"]["minimumBuild"] == 18362, "Windows target must be x64, Windows 10 1903+")
    require(manifest["tools"]["wix"]["version"] == "7.0.0", "WiX must match the reviewed 7.0.0 authoring")
    runtime = manifest["tools"]["vcRedist"]["runtimeDlls"]
    require({"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"} <= runtime.keys(), "Pinned app-local Microsoft CRT hashes are required")
    for name, digest in runtime.items():
        require(re.fullmatch(r"[A-Za-z0-9_]+\.dll", name) and re.fullmatch(r"[a-f0-9]{64}", digest), "Invalid app-local Microsoft CRT hash")
    archives = manifest["qt"]["archives"] + list(manifest["tools"].values()) + [manifest["mpv"], manifest["tracy"]] + manifest["wix"]["extensions"]
    names = set()
    for archive in archives:
        require(re.fullmatch(r"[a-f0-9]{64}", archive["sha256"]), "Missing SHA256 for " + archive["name"])
        url = urllib.parse.urlsplit(archive["url"])
        require(url.scheme == "https" and url.hostname in {"download.qt.io", "github.com", "download.visualstudio.microsoft.com", "api.nuget.org", "nodejs.org"}, "Unexpected dependency URL")
        require(pathlib.PurePath(archive["name"]).name == archive["name"] and archive["name"] not in names, "Invalid or duplicate archive filename")
        require(isinstance(archive["bytes"], int) and archive["bytes"] > 0, "Archive size is missing")
        names.add(archive["name"])


def check_source_evidence(report, manifest, manifest_sha):
    commit = report["buildCommit"]
    require(re.fullmatch(r"[a-f0-9]{40}", commit), "Evidence requires an immutable source commit")
    subprocess.run(["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=ROOT, check=True)
    tested_bytes = subprocess.check_output(["git", "show", commit + ":build-manifest.json"], cwd=ROOT)
    tested_manifest = json.loads(tested_bytes)
    require(report["appVersion"] == tested_manifest["appVersion"] and report["qtVersion"] == tested_manifest["qt"]["version"], "Evidence does not match the tested app and Qt build")
    require(report["buildManifestSha256"] == hashlib.sha256(tested_bytes).hexdigest(), "Evidence does not match the tested manifest bytes")
    promotion = "-rc." not in manifest["appVersion"] and re.fullmatch(re.escape(manifest["appVersion"]) + r"-rc\.[1-9][0-9]*", report["appVersion"])
    if promotion:
        old = dict(tested_manifest)
        new = dict(manifest)
        for key in ("appVersion", "packageVersion"):
            old.pop(key)
            new.pop(key)
        require(old == new, "Stable promotion changed qualified dependencies or release evidence")
        require(tuple(map(int, manifest["packageVersion"].split("."))) > tuple(map(int, tested_manifest["packageVersion"].split("."))), "Stable promotion must increment MSI packageVersion")
    else:
        require(report["appVersion"] == manifest["appVersion"] and report["buildManifestSha256"] == manifest_sha, "Evidence does not match this release manifest")
    changed = git("diff", "--name-only", commit, "HEAD").splitlines()
    require(all(path.startswith(("docs/", "quality/")) or (promotion and path == "build-manifest.json") for path in changed), "Source changed after the tested build")


def check_release(manifest, tag):
    require(tag == "v" + manifest["appVersion"], "Release tag must match the locked app version")
    qualification = manifest["qt"]["qualification"]
    require(qualification["qualified"] is True, "Qt release qualification is still incomplete (QTBUG-141377)")
    require(not git("status", "--porcelain"), "Release validation requires a clean checkout")
    require(qualification["fixRelease"] == manifest["qt"]["version"], "Qualified Qt release must match the locked archives")
    for field in ("releaseEvidenceUrl", "fixEvidenceUrl"):
        url = urllib.parse.urlsplit(qualification[field])
        require(url.scheme == "https" and url.hostname in {"qt.io", "www.qt.io", "code.qt.io", "bugreports.qt.io", "qt-project.atlassian.net", "download.qt.io"}, "Official Qt release and fix evidence are required")
    manifest_sha = hashlib.sha256((ROOT / "build-manifest.json").read_bytes()).hexdigest()
    memory = committed_json(qualification["memoryReportPath"])
    check_source_evidence(memory, manifest, manifest_sha)
    require(memory["issue"] == "QTBUG-141377" and memory["passed"] is True, "A passing Qt memory qualification report is required")
    require(len(memory["scenarios"]) >= 2, "Measure both navigation and long playback scenarios")
    for scenario in memory["scenarios"]:
        require(scenario["name"] and scenario["durationSeconds"] >= 3600, "Memory scenarios must run for at least one hour")
        for key in ("durationSeconds", "rssBaselineBytes", "rssPeakBytes", "rssFinalBytes", "limitBytes"):
            value = scenario[key]
            require(isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0, "Memory observations must be finite and positive")
        require(scenario["rssPeakBytes"] >= max(scenario["rssBaselineBytes"], scenario["rssFinalBytes"]), "Peak RSS is inconsistent")
        require(max(scenario["rssPeakBytes"], scenario["rssFinalBytes"]) <= scenario["limitBytes"], "Memory scenario exceeds its agreed RSS limit")
    contract = committed_json("contracts/desktop-api-contract.json")
    require(contract["status"] == "final", "Server/Web API handoff is still provisional")
    for field in ("serverVersion", "serverCommit", "webVersion", "webCommit", "apiRevision", "fixtureSha256"):
        require(contract.get(field), "Final API handoff is missing " + field)
    require(re.fullmatch(r"[a-f0-9]{64}", contract["fixtureSha256"]), "Final API fixtures require a SHA256")
    for field in ("serverCommit", "webCommit"):
        require(re.fullmatch(r"[a-f0-9]{40}", contract[field]), "Final API handoff requires immutable " + field)
    for field in ("serverVersion", "webVersion"):
        require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", contract[field]), "Invalid final " + field)
    require(isinstance(contract["apiRevision"], str) and contract["apiRevision"].strip(), "Final API revision must be explicit")
    fixture_path = contract["fixturePath"]
    committed_json(fixture_path)
    require(hashlib.sha256((ROOT / fixture_path).read_bytes()).hexdigest() == contract["fixtureSha256"], "Final API fixture bytes do not match their SHA256")
    if "-rc." not in manifest["appVersion"]:
        acceptance = committed_json("quality/desktop-acceptance.json")
        check_source_evidence(acceptance, manifest, manifest_sha)
        require(acceptance["accepted"] is True and acceptance.get("acceptedBy") == "user", "Stable release requires recorded final user acceptance")
        subprocess.run(["git", "merge-base", "--is-ancestor", "HEAD", "origin/master"], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-tag")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "build-manifest.json").read_text(encoding="utf-8-sig"))
    check_manifest(manifest)
    if args.release_tag:
        check_release(manifest, args.release_tag)
    print("Build manifest valid" + ("; release qualification passed" if args.release_tag else "; release qualification has not been asserted"))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit("Build/release validation failed: " + str(error)) from error
