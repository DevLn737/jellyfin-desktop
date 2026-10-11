"""Validate locked dependencies; release mode requires committed qualification evidence."""
import argparse
import copy
import datetime
import hashlib
import json
import math
import pathlib
import re
import subprocess
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEMVER = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-rc\.([1-9][0-9]*))?\Z")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def evidence_path(relative):
    require(isinstance(relative, str) and relative and "\\" not in relative and ":" not in relative,
            "Evidence must be a repository-relative POSIX path")
    path = pathlib.PurePosixPath(relative)
    require(not path.is_absolute() and path.parts and ".." not in path.parts,
            "Evidence must be a repository-relative path")
    return path


def committed_bytes(relative):
    evidence_path(relative)
    subprocess.run(["git", "ls-files", "--error-unmatch", relative], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return (ROOT / relative).read_bytes()


def committed_json(relative):
    return json.loads(committed_bytes(relative).decode("utf-8-sig"))


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
    if manifest["qt"]["qualification"]["qualified"] is True:
        check_release_proof(manifest)


def check_qualification_lock(manifest):
    qualification = manifest["qt"]["qualification"]
    require(qualification["fixRelease"] == manifest["qt"]["version"], "Qualified Qt release must match the locked archives")
    for field in ("releaseEvidenceUrl", "fixEvidenceUrl"):
        url = urllib.parse.urlsplit(qualification[field])
        require(url.scheme == "https" and url.hostname in {"qt.io", "www.qt.io", "code.qt.io", "bugreports.qt.io", "qt-project.atlassian.net", "download.qt.io"},
                "Official Qt release and fix evidence are required")
    for field in ("memoryReportPath", "memoryProtocolPath", "releaseProofPath"):
        evidence_path(qualification[field])


def check_release_metadata(metadata, version, observed):
    require(isinstance(metadata, dict) and metadata.get("name") == version and metadata.get("released") is True,
            "Official Qt metadata must mark the locked version as released")
    release_date = metadata.get("releaseDate")
    require(isinstance(release_date, str) and re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", release_date),
            "Official Qt release date is required")
    require(datetime.date.fromisoformat(release_date) <= observed.date(), "Official Qt release date is still in the future")


def check_release_proof(manifest, verify_live=False):
    qualification = manifest["qt"]["qualification"]
    url = qualification["releaseEvidenceUrl"]
    parts = urllib.parse.urlsplit(url)
    require(parts.scheme == "https" and parts.hostname in {"qt-project.atlassian.net", "bugreports.qt.io"}
            and re.fullmatch(r"/rest/api/2/version/[0-9]+", parts.path) and not parts.query and not parts.fragment
            and parts.port in (None, 443) and not parts.username and not parts.password, "Qt release proof requires the exact official Jira version endpoint")
    proof = committed_json(qualification["releaseProofPath"])
    require(proof["releaseEvidenceUrl"] == url, "Qt release proof differs from its locked evidence URL")
    observed = datetime.datetime.fromisoformat(proof["observedUtc"].replace("Z", "+00:00"))
    require(observed.tzinfo is not None and observed.utcoffset() == datetime.timedelta(0), "Qt release proof requires a UTC observation time")
    now = datetime.datetime.now(datetime.timezone.utc)
    require(observed <= now, "Qt release proof observation time is in the future")
    check_release_metadata(proof["metadata"], manifest["qt"]["version"], observed)
    if verify_live:
        request = urllib.request.Request(url, headers={"Accept": "application/json", "User-Agent": "JellyfinDesktopRelease/2"})
        with urllib.request.urlopen(request, timeout=10) as response:
            final = urllib.parse.urlsplit(response.geturl())
            require(final.scheme == "https" and final.hostname in {"qt-project.atlassian.net", "bugreports.qt.io"}
                    and final.path == parts.path and not final.query and not final.fragment,
                    "Official Qt release endpoint redirected outside its version metadata")
            payload = response.read(256 * 1024 + 1)
        require(len(payload) <= 256 * 1024, "Official Qt release metadata response is too large")
        metadata = json.loads(payload)
        check_release_metadata(metadata, manifest["qt"]["version"], now)
        require(metadata["releaseDate"] == proof["metadata"]["releaseDate"], "Committed Qt release proof differs from current official metadata")


def check_source_evidence(report, manifest, manifest_sha):
    commit = report["buildCommit"]
    require(re.fullmatch(r"[a-f0-9]{40}", commit), "Evidence requires an immutable source commit")
    subprocess.run(["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=ROOT, check=True)
    tested_bytes = subprocess.check_output(["git", "show", commit + ":build-manifest.json"], cwd=ROOT)
    tested_manifest = json.loads(tested_bytes)
    require(report["appVersion"] == tested_manifest["appVersion"] and report["qtVersion"] == tested_manifest["qt"]["version"], "Evidence does not match the tested app and Qt build")
    require(report["buildManifestSha256"] == hashlib.sha256(tested_bytes).hexdigest(), "Evidence does not match the tested manifest bytes")
    promotion = bool("-rc." not in manifest["appVersion"] and re.fullmatch(re.escape(manifest["appVersion"]) + r"-rc\.[1-9][0-9]*", report["appVersion"]))
    tested_qualification = tested_manifest["qt"]["qualification"]
    current_qualification = manifest["qt"]["qualification"]
    transition = tested_qualification["qualified"] is False and current_qualification["qualified"] is True
    old = copy.deepcopy(tested_manifest)
    new = copy.deepcopy(manifest)
    if transition:
        # Official fix evidence and the protocol/report paths must already have
        # been locked before measuring. Only this result flag may change later.
        check_qualification_lock(tested_manifest)
        old["qt"]["qualification"]["qualified"] = True
    if promotion:
        for key in ("appVersion", "packageVersion"):
            old.pop(key)
            new.pop(key)
        require(tuple(map(int, manifest["packageVersion"].split("."))) > tuple(map(int, tested_manifest["packageVersion"].split("."))), "Stable promotion must increment MSI packageVersion")
    require(old == new, "Stable promotion changed qualified dependencies or release evidence" if promotion
            else "Qualification transition changed dependencies or release evidence")
    if not promotion:
        require(report["appVersion"] == manifest["appVersion"], "Evidence does not match this release app version")
    if not promotion and not transition:
        require(report["buildManifestSha256"] == manifest_sha, "Evidence does not match this release manifest")
    changed = git("diff", "--name-only", commit, "HEAD").splitlines()
    require(all(path.startswith(("docs/", "quality/")) or ((promotion or transition) and path == "build-manifest.json") for path in changed),
            "Source changed after the tested build")


def finite_number(value, minimum=0):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= minimum


def check_memory_evidence(memory, manifest):
    require(memory["issue"] == "QTBUG-141377" and memory["passed"] is True, "A passing Qt memory qualification report is required")
    require(memory.get("processTreeIncluded") is True, "Memory qualification must include the owned native/WebEngine process tree")
    protocol_path = manifest["qt"]["qualification"]["memoryProtocolPath"]
    protocol_bytes = committed_bytes(protocol_path)
    tested_protocol = subprocess.check_output(["git", "show", memory["buildCommit"] + ":" + protocol_path], cwd=ROOT)
    require(protocol_bytes == tested_protocol, "Memory protocol changed after the tested build")
    protocol_sha = hashlib.sha256(protocol_bytes).hexdigest()
    require(memory["protocolSha256"] == protocol_sha, "Memory report does not match the precommitted protocol bytes")
    protocol = json.loads(protocol_bytes)
    scope = "windows-owned-process-tree-rss"
    require(protocol["schemaVersion"] == 1 and protocol["scope"] == scope, "Unsupported memory protocol or process scope")
    expected = {"navigation-svg", "long-playback"}
    required = protocol["requiredScenarios"]
    require(isinstance(required, list) and len(required) == 2 and set(required) == expected,
            "Memory protocol requires distinct navigation-svg and long-playback scenarios")
    require(finite_number(protocol["minimumDurationSeconds"], 3600), "Memory protocol must require at least one hour per scenario")
    require(finite_number(protocol["warmupSeconds"]) and finite_number(protocol["sampleIntervalSeconds"], 0.001), "Invalid memory sampling protocol")
    for key in ("absoluteRssLimitBytes", "finalGrowthLimitBytes"):
        require(finite_number(protocol[key], 1), "Memory limits must be predeclared finite positive bytes")
    raw_bytes = committed_bytes(memory["rawSamplesPath"])
    require(memory["rawSamplesSha256"] == hashlib.sha256(raw_bytes).hexdigest(), "Memory raw samples do not match their SHA256")
    raw = json.loads(raw_bytes)
    require(raw["schemaVersion"] == 1 and raw["scope"] == scope, "Memory raw samples have the wrong schema or process scope")

    def by_scenario(items):
        require(isinstance(items, list) and len(items) == 2, "Memory evidence requires exactly two distinct scenarios")
        require(all(isinstance(item, dict) and item.get("name") in expected for item in items), "Memory evidence has an unknown scenario")
        result = {item["name"]: item for item in items}
        require(set(result) == expected, "Memory evidence requires both navigation-svg and long-playback")
        return result

    observations = by_scenario(raw["scenarios"])
    summaries = by_scenario(memory["scenarios"])
    for scenario_name in sorted(expected):
        samples = observations[scenario_name]["samples"]
        require(isinstance(samples, list) and len(samples) >= 2, "Memory qualification requires raw observation samples")
        previous = -1
        for sample in samples:
            require(finite_number(sample["elapsedSeconds"]) and sample["elapsedSeconds"] > previous, "Memory samples must have strictly increasing finite elapsed times")
            require(finite_number(sample["rssBytes"], 1), "Memory RSS observations must be finite positive bytes")
            require(type(sample["processCount"]) is int and sample["processCount"] > 0, "Memory samples must count the owned process tree")
            previous = sample["elapsedSeconds"]
        require(samples[-1]["elapsedSeconds"] >= protocol["minimumDurationSeconds"], "Memory scenarios must run for at least one hour")
        baseline = samples[0]["rssBytes"]
        peak = max(sample["rssBytes"] for sample in samples)
        final = samples[-1]["rssBytes"]
        derived = {"durationSeconds": samples[-1]["elapsedSeconds"], "rssBaselineBytes": baseline,
                   "rssPeakBytes": peak, "rssFinalBytes": final, "limitBytes": protocol["absoluteRssLimitBytes"]}
        summary = summaries[scenario_name]
        for key, value in derived.items():
            require(finite_number(summary[key], 1) and summary[key] == value, "Memory summary differs from raw observations or predeclared limit: " + key)
        require(peak <= protocol["absoluteRssLimitBytes"], "Memory scenario exceeds its predeclared RSS limit")
        require(final - baseline <= protocol["finalGrowthLimitBytes"], "Memory scenario exceeds its predeclared final-growth limit")


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
    check_qualification_lock(manifest)
    check_release_proof(manifest, verify_live=True)
    manifest_sha = hashlib.sha256((ROOT / "build-manifest.json").read_bytes()).hexdigest()
    memory = committed_json(qualification["memoryReportPath"])
    check_source_evidence(memory, manifest, manifest_sha)
    check_memory_evidence(memory, manifest)
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
