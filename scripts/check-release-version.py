"""Ensure each published installer has a strictly newer Windows Installer version."""
import json
import os
import pathlib
import re
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = json.loads((ROOT / "build-manifest.json").read_text(encoding="utf-8-sig"))
REPOSITORY = MANIFEST["releaseRepository"]


def api_json(url):
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "JellyfinDesktopRelease/2"}
    if os.environ.get("GH_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
        payload = response.read(4 * 1024 * 1024 + 1)
        if len(payload) > 4 * 1024 * 1024:
            raise ValueError("Release API response is too large")
        return json.loads(payload)


def version_tuple(value):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value):
        raise ValueError("Invalid prior MSI package version")
    return tuple(map(int, value.split(".")))


def main():
    current_tag = "v" + MANIFEST["appVersion"]
    prior_versions = []
    for page in range(1, 101):
        releases = api_json(f"https://api.github.com/repos/{REPOSITORY}/releases?per_page=100&page={page}")
        if not releases:
            break
        for release in releases:
            if release["draft"]:
                continue
            if release["tag_name"] == current_tag:
                raise ValueError("This version is already published; existing releases are immutable")
            asset = next((item for item in release["assets"] if item["name"] == "build-manifest.json"), None)
            if asset:
                url = asset["browser_download_url"]
                expected = f"https://github.com/{REPOSITORY}/releases/download/"
                if not url.startswith(expected) or not url.endswith("/build-manifest.json"):
                    raise ValueError("Unexpected prior release manifest URL")
                with urllib.request.urlopen(url, timeout=30) as response:
                    payload = response.read(256 * 1024 + 1)
                if len(payload) > 256 * 1024:
                    raise ValueError("Prior release manifest is too large")
                prior_versions.append(version_tuple(json.loads(payload)["packageVersion"]))
            else:
                # Historical 1.x packages used the three-part tag version in MSI.
                old = re.match(r"^v?([0-9]+\.[0-9]+\.[0-9]+)(?:-|$)", release["tag_name"])
                if old:
                    if int(old[1].split(".")[0]) >= 2:
                        raise ValueError("A migrated release is missing its MSI build manifest")
                    prior_versions.append(version_tuple(old[1]))
        if len(releases) < 100:
            break
    else:
        raise ValueError("Release history exceeds the verification limit")
    current = version_tuple(MANIFEST["packageVersion"])
    if prior_versions and current <= max(prior_versions):
        raise ValueError("MSI packageVersion must be strictly higher than every published package")
    print("Monotonic Windows Installer version verified: " + MANIFEST["packageVersion"])


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit("Release version check failed: " + str(error)) from error
