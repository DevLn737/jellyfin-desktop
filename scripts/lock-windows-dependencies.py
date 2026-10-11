#!/usr/bin/env python3
"""Resolve official Windows archives and hash them into a reviewable manifest.

This maintenance command writes a candidate under build/, never changes the
release manifest in place, and does not claim that a Qt release is qualified.
"""
import argparse
import concurrent.futures
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
    qt = qt_archives(args.qt_version)
    cmake_url = "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip"
    cmake_hashes = request("https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-SHA-256.txt").read().decode("ascii")
    cmake_sha = next(line.split()[0] for line in cmake_hashes.splitlines() if line.endswith("cmake-3.31.6-windows-x86_64.zip"))
    tools = {
        "node": {"version": "24.21.0", "name": "node-v24.21.0-win-x64.zip", "url": "https://nodejs.org/dist/v24.21.0/node-v24.21.0-win-x64.zip", "sha256": "158f7685b44de51f6c0df1d153526cbcd3e1bc739a8dfc607721cef75de9e541"},
        "cmake": {"version": "3.31.6", "name": "cmake-3.31.6-windows-x86_64.zip", "url": cmake_url, "sha256": cmake_sha},
        "ninja": {"version": "1.13.2", "name": "ninja-win.zip", "url": "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip", "sha256": "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65"},
        "wix": {"version": "7.0.0", "name": "wix-cli-x64.msi", "url": "https://github.com/wixtoolset/wix/releases/download/v7.0.0/wix-cli-x64.msi", "sha256": "16ff3857255e0ffa118d90e6d3a20e47499e55cda109853b5aac4700735f0293"},
        "vcRedist": {"name": "VC_redist.x64.exe", "url": "https://download.visualstudio.microsoft.com/download/pr/bd1c8d9d-ba95-4eee-bc6e-df1fcc876373/CC0FF0EB1DC3F5188AE6300FAEF32BF5BEEBA4BDD6E8E445A9184072096B713B/VC_redist.x64.exe"},
    }
    mpv = {"version": "20261011-git-accb7b2821", "commit": "accb7b2821ecf0cb04cf3579ccd98bc8f751f61f", "architecture": "x86_64", "name": "mpv-dev-x86_64-20261011-git-accb7b2821.7z", "url": "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20261011/mpv-dev-x86_64-20261011-git-accb7b2821.7z", "sha256": "cf63b4ce66e03e5e70acf2648cbc3177656c77c5b9583e16d83efc58801182b7"}
    tracy = {"version": "0.14.1", "tag": "v0.14.1", "commit": "30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c", "name": "tracy-0.14.1.tar.gz", "url": "https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz"}
    extensions = [{"id": name, "version": "7.0.0", "name": name.lower() + ".7.0.0.nupkg",
                   "url": "https://api.nuget.org/v3-flatcontainer/" + name.lower() + "/7.0.0/" + name.lower() + ".7.0.0.nupkg"}
                  for name in ("WixToolset.Util.wixext", "WixToolset.BootstrapperApplications.wixext")]
    all_archives = qt + list(tools.values()) + [mpv, tracy] + extensions
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda item: lock_archive(item, cache), all_archives))
    tools["vcRedist"]["version"] = "14.44.35211.0"
    # These DLL hashes were independently verified against the minimum-amd64
    # CAB extracted from the locked Microsoft redistributable. Keep that reviewed
    # payload lock when refreshing Qt archives; a CRT update requires new proof.
    current = json.loads((ROOT / "build-manifest.json").read_text(encoding="utf-8-sig"))
    if current["tools"]["vcRedist"]["sha256"] != tools["vcRedist"]["sha256"]:
        raise ValueError("A new Microsoft CRT requires independently verified payload hashes")
    tools["vcRedist"]["runtimeDlls"] = current["tools"]["vcRedist"]["runtimeDlls"]
    manifest = {
        "schemaVersion": 1, "appVersion": "2.0.0-rc.1", "packageVersion": "2.0.1",
        "sources": {"forkBaseCommit": "955a60349cfb6837d635af19e8dd192d7218109f", "qtPortCommit": "2cb4a4456fd29b5b62d825ef0e5df93ed6913328"},
        "releaseRepository": "DevLn737/jellyfin-desktop", "windows": {"architecture": "x64", "minimumBuild": 18362, "msvcToolset": "v143", "msvcToolsVersion": "14.44.35207", "windowsSdkVersion": "10.0.26100.0", "runner": "windows-2022"},
        "qt": {"version": args.qt_version, "architecture": "win64_msvc2022_64", "archives": qt,
               "qualification": {"issue": "QTBUG-141377", "qualified": False, "fixRelease": "", "releaseEvidenceUrl": "", "fixEvidenceUrl": "", "memoryReportPath": ""}},
        "tools": tools, "mpv": mpv, "tracy": tracy,
        "wix": {"eulaId": "wix7", "extensions": extensions},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Review candidate at {args.output}; Qt release qualification remains false.", flush=True)


if __name__ == "__main__":
    main()
