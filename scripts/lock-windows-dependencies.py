#!/usr/bin/env python3
"""Resolve official Windows archives and hash them into a reviewable manifest.

This maintenance command writes a candidate under build/, never changes the
release manifest in place, and does not claim that a Qt release is qualified.
"""
import argparse
import concurrent.futures
import copy
import hashlib
import json
import pathlib
import urllib.request
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent


def request(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "JellyfinDesktopBuild/2"}), timeout=120)


def qt_archives(version):
    compact = version.replace(".", "")
    root = "https://download.qt.io/online/qtsdkrepository/windows_x86/"
    desktop = f"{root}desktop/qt6_{compact}/qt6_{compact}_msvc2022_64/"
    packages = ET.fromstring(request(desktop + "Updates.xml").read())
    selected = []
    for package in packages.findall("PackageUpdate"):
        name = package.findtext("Name", "")
        if name != f"qt.qt6.{compact}.win64_msvc2022_64" and not any(
            name == f"qt.qt6.{compact}.addons.{module}.win64_msvc2022_64"
            for module in ("qtwebchannel", "qtpositioning", "qtshadertools", "qtserialport")
        ):
            continue
        for archive in package.findtext("DownloadableArchives", "").split(","):
            archive = archive.strip()
            if not archive or archive.startswith("qtdoc-"):
                continue
            selected.append({"name": archive, "url": desktop + name + "/" + package.findtext("Version") + archive})
    extension = f"{root}extensions/qtwebengine/{compact}/msvc2022_64/"
    engine = ET.fromstring(request(extension + "Updates.xml").read())
    for package in engine.findall("PackageUpdate"):
        if package.findtext("Name") == f"extensions.qtwebengine.{compact}.win64_msvc2022_64":
            archive = package.findtext("DownloadableArchives")
            selected.append({"name": archive, "url": extension + package.findtext("Name") + "/" + package.findtext("Version") + archive})
    if len(selected) != 12:
        raise RuntimeError(f"Expected twelve Qt archives, found {len(selected)}; review repository changes")
    for archive in selected:
        archive["officialSha1"] = request(archive["url"] + ".sha1").read().decode("ascii").strip()
    return selected


def lock_archive(archive, cache):
    target = cache / archive["name"]
    if not target.exists():
        partial = target.with_suffix(target.suffix + ".partial")
        with request(archive["url"]) as response, partial.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        partial.replace(target)
    sha1, sha256 = hashlib.sha1(), hashlib.sha256()
    with target.open("rb") as data:
        while block := data.read(1024 * 1024):
            sha1.update(block)
            sha256.update(block)
    if archive.get("officialSha1") and sha1.hexdigest() != archive["officialSha1"]:
        raise RuntimeError(f"Official Qt checksum mismatch: {archive['name']}")
    if archive.get("sha256") and sha256.hexdigest() != archive["sha256"]:
        raise RuntimeError(f"Official release checksum mismatch: {archive['name']}")
    archive["sha256"] = sha256.hexdigest()
    archive["bytes"] = target.stat().st_size
    print(f"Verified {archive['name']}: {archive['sha256']}", flush=True)
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-version", default="6.11.2")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("build/build-manifest.candidate.json"))
    args = parser.parse_args()
    cache = pathlib.Path("build/dependency-archives")
    cache.mkdir(parents=True, exist_ok=True)
    # Qt patch releases reuse archive filenames. Keep their reviewed bytes in
    # separate directories so refreshing Qt cannot overwrite a baseline lock.
    qt_cache = cache / ("qt-" + args.qt_version)
    qt_cache.mkdir(parents=True, exist_ok=True)
    qt = qt_archives(args.qt_version)
    current = json.loads((ROOT / "build-manifest.json").read_text(encoding="utf-8-sig"))
    if not current["tools"].get("vulkanRuntime"):
        raise ValueError("Qt refresh requires the independently reviewed Vulkan runtime and payload lock")
    # A Qt refresh does not also update tools, media libraries, product versions
    # or payload locks. Verify all existing immutable archive locks unchanged.
    manifest = copy.deepcopy(current)
    non_qt = list(manifest["tools"].values()) + [manifest["mpv"], manifest["tracy"]] + manifest["wix"]["extensions"]
    archive_jobs = [(archive, qt_cache) for archive in qt] + [(archive, cache) for archive in non_qt]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda job: lock_archive(*job), archive_jobs))
    manifest["qt"] = {
        "version": args.qt_version, "architecture": current["qt"]["architecture"], "archives": qt,
        "qualification": {"issue": "QTBUG-141377", "qualified": False, "fixRelease": "", "releaseEvidenceUrl": "", "fixEvidenceUrl": "", "memoryReportPath": ""},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Review candidate at {args.output}; Qt release qualification remains false.", flush=True)


if __name__ == "__main__":
    main()
