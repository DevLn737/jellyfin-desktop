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


def check_vulkan_runtime(runtime):
    require(isinstance(runtime, dict), "Pinned x64 Vulkan runtime is required")
    version = runtime.get("version")
    require(isinstance(version, str) and re.fullmatch(r"1\.[0-9]+\.[0-9]+\.[0-9]+", version), "Invalid Vulkan runtime version")
    archive_name = f"VulkanRT-X64-{version}-Components.zip"
    require(runtime.get("name") == archive_name, "Vulkan runtime must be the official x64 components ZIP")
    require(runtime.get("url") == f"https://sdk.lunarg.com/sdk/download/{version}/windows/{archive_name}",
            "Vulkan runtime must use its exact official LunarG download URL")
    require(isinstance(runtime.get("commit"), str) and re.fullmatch(r"[a-f0-9]{40}", runtime["commit"]),
            "Vulkan runtime requires an immutable source commit")
    archive_root = archive_name.removesuffix(".zip")
    for key, name, path in (("runtimeDll", "vulkan-1.dll", f"{archive_root}/x64/vulkan-1.dll"),
                            ("license", "VulkanRT-License.txt", f"{archive_root}/VulkanRT-License.txt")):
        payload = runtime.get(key)
        require(isinstance(payload, dict), "Pinned Vulkan " + key + " payload is required")
        # Exact archive-relative paths reject traversal, drive/UNC paths,
        # backslash aliases and the archive's separate x86 payload.
        require(payload.get("name") == name and payload.get("path") == path, "Unsafe or non-x64 Vulkan " + key + " path")
        require(isinstance(payload.get("sha256"), str) and re.fullmatch(r"[a-f0-9]{64}", payload["sha256"]),
                "Missing SHA256 for Vulkan " + key + " payload")
        require(type(payload.get("bytes")) is int and payload["bytes"] > 0, "Vulkan " + key + " payload size is missing")


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
    check_vulkan_runtime(manifest["tools"].get("vulkanRuntime"))
    archives = manifest["qt"]["archives"] + list(manifest["tools"].values()) + [manifest["mpv"], manifest["tracy"]] + manifest["wix"]["extensions"]
    names = set()
    for archive in archives:
        require(re.fullmatch(r"[a-f0-9]{64}", archive["sha256"]), "Missing SHA256 for " + archive["name"])
        url = urllib.parse.urlsplit(archive["url"])
        require(url.scheme == "https" and url.hostname in {"download.qt.io", "github.com", "download.visualstudio.microsoft.com", "api.nuget.org", "nodejs.org", "sdk.lunarg.com"}, "Unexpected dependency URL")
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


def finite_number(value, minimum=0):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= minimum


def check_final_contract(contract, fixture):
    require(contract["status"] == "final", "Server/Web API contract is still provisional")
    for field in ("serverVersion", "serverCommit", "webVersion", "webCommit", "apiRevision", "fixtureSha256"):
        require(contract.get(field), "Final API contract is missing " + field)
    require(re.fullmatch(r"[a-f0-9]{64}", contract["fixtureSha256"]), "Final API fixtures require a SHA256")
    for field in ("serverCommit", "webCommit"):
        require(re.fullmatch(r"[a-f0-9]{40}", contract[field]), "Final API contract requires immutable " + field)
        require(fixture[field] == contract[field], "API fixtures differ from the pinned " + field)
    for field in ("serverVersion", "webVersion"):
        require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", contract[field]), "Invalid final " + field)
    require(isinstance(contract["apiRevision"], str) and contract["apiRevision"].strip(), "Final API revision must be explicit")
    require(contract.get("apiRevisionProvenance"), "Explain the Desktop contract revision provenance")
    require(fixture["contractRevision"] == contract["apiRevision"], "API fixtures differ from the finalized contract revision")
    evidence = contract["evidence"]
    require(evidence["immutableDeploymentPinsVerified"] is True, "Final API contract requires verified deployment pins")
    require(fixture["provenance"] == "source-derived-synthetic" and fixture["capturedFromLiveServer"] is False,
            "Source-derived API fixtures must not claim live capture")
    require(evidence["capturedFromLiveServer"] is False, "Contract fixture provenance must match the synthetic fixtures")
    # A source-reviewed fallback can qualify a contract without a server-agent
    # handoff. This pinned final-handoff contract must retain its actual proof.
    if evidence["kind"].startswith("verified-final-server-handoff"):
        require(evidence["finalServerAgentHandoffReceived"] is True and evidence["handoffFileHashesVerified"] is True,
                "Final server-agent handoff has not been verified")
        require(re.fullmatch(r"[a-f0-9]{64}", evidence["handoffArchiveSha256"]), "Final handoff archive requires a SHA256")
        server_contract_sha = evidence["handoffFiles"]["server-contract.json"]
        require(re.fullmatch(r"[a-f0-9]{64}", server_contract_sha) and fixture["handoffServerContractSha256"] == server_contract_sha,
                "API fixtures differ from the verified server handoff")
    drift = contract["syncPlay"]["driftAcceptance"]
    require(drift["protocolGuaranteesMaximumDrift"] is False and drift["sourceDefaultsAreNotAcceptanceLimits"] is True,
            "Native drift qualification must be separate from protocol defaults")
    require(finite_number(drift["absoluteDriftMillisecondsExclusiveMaximum"], minimum=1), "Final contract requires a finite positive drift limit")
    require(drift["correctionEnabled"] is True and drift["baseRates"] and drift["sampleAfterSeconds"], "Final contract requires mixed-client timing scenarios")
    require(all(finite_number(value, minimum=0.01) for value in drift["baseRates"] + drift["sampleAfterSeconds"]), "Invalid final drift scenario values")


def check_native_acceptance(acceptance, contract):
    require(acceptance["accepted"] is True and acceptance.get("acceptedBy") == "user", "Stable release requires recorded final user acceptance")
    require(acceptance.get("nativeAcceptancePerformed") is True, "Browser acceptance does not establish Windows native acceptance")
    for field in ("apiRevision", "serverCommit", "webCommit"):
        require(acceptance.get(field) == contract[field], "Native acceptance differs from the final API " + field)
    sync_play = acceptance["syncPlay"]
    require(sync_play["composition"] == "windows-desktop-and-web" and sync_play["correctionEnabled"] is True and sync_play["coldSecondClient"] is True,
            "Native acceptance requires mixed Desktop/Web clients and correction")
    required_cases = {"pause-seek", "rejoin-reconnect", "legacy-409", "personal-speed-restoration", "independent-audio-subtitles", "direct-play", "hls"}
    require(required_cases <= set(sync_play["mixedClientCases"]), "Native acceptance is missing an integrated mixed-client case")
    drift = contract["syncPlay"]["driftAcceptance"]
    observed = set()
    for sample in sync_play["driftSamples"]:
        require(finite_number(sample["baseRate"], minimum=0.01) and finite_number(sample["afterSeconds"], minimum=0.01)
                and finite_number(sample["absoluteDriftMilliseconds"]), "Native drift observations must be finite numbers")
        require(sample["absoluteDriftMilliseconds"] < drift["absoluteDriftMillisecondsExclusiveMaximum"], "Native drift observation exceeds the final contract's exclusive limit")
        observed.add((sample["baseRate"], sample["afterSeconds"]))
    required_samples = {(rate, seconds) for rate in drift["baseRates"] for seconds in drift["sampleAfterSeconds"]}
    require(required_samples <= observed, "Native acceptance is missing a final-contract rate/time observation")


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
    fixture_path = contract["fixturePath"]
    fixture = committed_json(fixture_path)
    check_final_contract(contract, fixture)
    require(hashlib.sha256((ROOT / fixture_path).read_bytes()).hexdigest() == contract["fixtureSha256"], "Final API fixture bytes do not match their SHA256")
    if "-rc." not in manifest["appVersion"]:
        acceptance = committed_json("quality/desktop-acceptance.json")
        check_source_evidence(acceptance, manifest, manifest_sha)
        check_native_acceptance(acceptance, contract)
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
