"""Release regressions: qualification blocks, invalid locks, and RC promotion evidence."""
import copy
import hashlib
import importlib.util
import io
import json
import pathlib
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("release_validation", ROOT / "scripts/validate-build-manifest.py")
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)
lock_spec = importlib.util.spec_from_file_location("dependency_lock", ROOT / "scripts/lock-windows-dependencies.py")
dependency_lock = importlib.util.module_from_spec(lock_spec)
lock_spec.loader.exec_module(dependency_lock)
MANIFEST = json.loads((ROOT / "build-manifest.json").read_text(encoding="utf-8-sig"))
CONTRACT = json.loads((ROOT / "contracts/desktop-api-contract.json").read_text(encoding="utf-8-sig"))
FIXTURE = json.loads((ROOT / CONTRACT["fixturePath"]).read_text(encoding="utf-8-sig"))


class ReleaseValidationTest(unittest.TestCase):
    def prepared_manifest(self):
        # Test-only source metadata, never a real qualification report or lock.
        manifest = copy.deepcopy(MANIFEST)
        manifest["qt"]["qualification"].update(
            qualified=False, fixRelease=manifest["qt"]["version"],
            releaseEvidenceUrl="https://qt-project.atlassian.net/rest/api/2/version/16945",
            fixEvidenceUrl="https://qt-project.atlassian.net/test-only-fix-evidence",
            memoryReportPath="quality/test-only-memory.json", memoryProtocolPath="quality/memory-protocol.json",
            releaseProofPath="quality/test-only-release-proof.json")
        return manifest

    def source_report(self, manifest):
        tested_bytes = (json.dumps(manifest, indent=2) + "\n").encode()
        report = {"appVersion": manifest["appVersion"], "qtVersion": manifest["qt"]["version"],
                  "buildCommit": "1" * 40, "buildManifestSha256": hashlib.sha256(tested_bytes).hexdigest()}
        return report, tested_bytes

    def check_source(self, tested, current, changed="build-manifest.json\nquality/test-only-memory.json"):
        report, tested_bytes = self.source_report(tested)
        current_sha = hashlib.sha256(json.dumps(current).encode()).hexdigest()
        with patch.object(validation.subprocess, "check_output", return_value=tested_bytes), \
             patch.object(validation.subprocess, "run"), patch.object(validation, "git", return_value=changed):
            validation.check_source_evidence(report, current, current_sha)

    def test_development_lock_cannot_publish(self):
        manifest = copy.deepcopy(MANIFEST)
        manifest["qt"]["qualification"]["qualified"] = False
        with self.assertRaisesRegex(ValueError, "qualification is still incomplete"):
            validation.check_release(manifest, "v" + manifest["appVersion"])

    def test_invalid_version_or_lock_is_rejected(self):
        for field, value in (("appVersion", "2.0.0-rc.01"), ("appVersion", "02.0.0"), ("packageVersion", "2.0.65536")):
            manifest = copy.deepcopy(MANIFEST)
            manifest[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validation.check_manifest(manifest)
        manifest = copy.deepcopy(MANIFEST)
        manifest["qt"]["archives"][0]["sha256"] = "pending"
        with self.assertRaisesRegex(ValueError, "Missing SHA256"):
            validation.check_manifest(manifest)

    def test_vulkan_runtime_requires_loader_license_source_and_safe_x64_paths(self):
        validation.check_manifest(MANIFEST)
        manifest = copy.deepcopy(MANIFEST)
        del manifest["tools"]["vulkanRuntime"]
        with self.assertRaisesRegex(ValueError, "Vulkan runtime is required"):
            validation.check_manifest(manifest)
        for key in ("runtimeDll", "license"):
            manifest = copy.deepcopy(MANIFEST)
            del manifest["tools"]["vulkanRuntime"][key]
            with self.subTest(missing=key), self.assertRaisesRegex(ValueError, "payload is required"):
                validation.check_manifest(manifest)
            manifest = copy.deepcopy(MANIFEST)
            del manifest["tools"]["vulkanRuntime"][key]["sha256"]
            with self.subTest(hash=key), self.assertRaisesRegex(ValueError, "Missing SHA256"):
                validation.check_manifest(manifest)
        original = MANIFEST["tools"]["vulkanRuntime"]["runtimeDll"]["path"]
        for unsafe in ("../vulkan-1.dll", "C:/Windows/System32/vulkan-1.dll", original.replace("/x64/", "/x86/"), original.replace("/", "\\")):
            manifest = copy.deepcopy(MANIFEST)
            manifest["tools"]["vulkanRuntime"]["runtimeDll"]["path"] = unsafe
            with self.subTest(path=unsafe), self.assertRaisesRegex(ValueError, "Unsafe or non-x64"):
                validation.check_manifest(manifest)
        for field, value in (("commit", "moving-main"), ("url", "https://sdk.lunarg.com.evil.example/runtime.zip")):
            manifest = copy.deepcopy(MANIFEST)
            manifest["tools"]["vulkanRuntime"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validation.check_manifest(manifest)

    def test_qt_lock_refresh_keeps_and_verifies_the_reviewed_vulkan_archive(self):
        # Exercise candidate generation without downloads or output files. The
        # archive-lock boundary receives Vulkan as well as the other archives.
        reviewed = MANIFEST["tools"]["vulkanRuntime"]
        known = {item["name"]: item for item in MANIFEST["qt"]["archives"] +
                 list(MANIFEST["tools"].values()) + [MANIFEST["mpv"], MANIFEST["tracy"]] + MANIFEST["wix"]["extensions"]}
        verified_names = []

        def verify_cached_archive(archive, cache):
            verified_names.append(archive["name"])
            pinned = known[archive["name"]]
            if archive["name"] == reviewed["name"]:
                self.assertEqual(archive, reviewed)
                self.assertIsNot(archive, reviewed)
            archive.update(sha256=pinned["sha256"], bytes=pinned["bytes"])
            return archive

        output = MagicMock()
        cmake_hash = (MANIFEST["tools"]["cmake"]["sha256"] + "  " + MANIFEST["tools"]["cmake"]["name"] + "\n").encode()
        args = SimpleNamespace(qt_version=MANIFEST["qt"]["version"], output=output)
        with patch.object(dependency_lock.argparse.ArgumentParser, "parse_args", return_value=args), \
             patch.object(dependency_lock, "qt_archives", return_value=copy.deepcopy(MANIFEST["qt"]["archives"])), \
             patch.object(dependency_lock, "request", return_value=io.BytesIO(cmake_hash)), \
             patch.object(dependency_lock, "lock_archive", side_effect=verify_cached_archive), patch("builtins.print"):
            dependency_lock.main()
        candidate = json.loads(output.write_text.call_args.args[0])
        self.assertIn(reviewed["name"], verified_names)
        self.assertEqual(candidate["tools"]["vulkanRuntime"], reviewed)
        self.assertFalse(candidate["qt"]["qualification"]["qualified"])
        validation.check_manifest(candidate)

    def test_rc_acceptance_only_promotes_unchanged_code_and_dependencies(self):
        tested = copy.deepcopy(MANIFEST)
        tested["appVersion"] = "2.0.0-rc.3"
        tested["packageVersion"] = "2.0.3"
        tested_bytes = (json.dumps(tested, indent=2) + "\n").encode()
        report = {"appVersion": tested["appVersion"], "qtVersion": tested["qt"]["version"],
                  "buildCommit": "1" * 40, "buildManifestSha256": hashlib.sha256(tested_bytes).hexdigest()}
        stable = copy.deepcopy(tested)
        stable["appVersion"] = "2.0.0"
        stable["packageVersion"] = "2.0.4"
        stable_sha = hashlib.sha256(json.dumps(stable).encode()).hexdigest()
        with patch.object(validation.subprocess, "check_output", return_value=tested_bytes), \
             patch.object(validation.subprocess, "run"), patch.object(validation, "git", return_value="build-manifest.json\nquality/desktop-acceptance.json"):
            validation.check_source_evidence(report, stable, stable_sha)
            changed_dependency = copy.deepcopy(stable)
            changed_dependency["qt"]["archives"][0]["sha256"] = "f" * 64
            with self.assertRaisesRegex(ValueError, "changed qualified dependencies"):
                validation.check_source_evidence(report, changed_dependency, stable_sha)
            report["buildManifestSha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "tested manifest bytes"):
                validation.check_source_evidence(report, stable, stable_sha)

    def test_intervening_application_change_invalidates_promotion(self):
        tested = copy.deepcopy(MANIFEST)
        tested["appVersion"] = "2.0.0-rc.1"
        tested_bytes = json.dumps(tested).encode()
        report = {"appVersion": tested["appVersion"], "qtVersion": tested["qt"]["version"],
                  "buildCommit": "2" * 40, "buildManifestSha256": hashlib.sha256(tested_bytes).hexdigest()}
        stable = copy.deepcopy(tested)
        stable["appVersion"] = "2.0.0"
        stable["packageVersion"] = "2.0.2"
        with patch.object(validation.subprocess, "check_output", return_value=tested_bytes), \
             patch.object(validation.subprocess, "run"), patch.object(validation, "git", return_value="src/player/PlayerComponent.cpp"):
            with self.assertRaisesRegex(ValueError, "Source changed after"):
                validation.check_source_evidence(report, stable, "unused")

    def test_only_false_to_true_qualification_result_can_follow_measurement(self):
        tested = self.prepared_manifest()
        current = copy.deepcopy(tested)
        current["qt"]["qualification"]["qualified"] = True
        self.check_source(tested, current)
        with self.assertRaisesRegex(ValueError, "changed dependencies or release evidence"):
            self.check_source(current, tested)
        with self.assertRaisesRegex(ValueError, "Source changed after"):
            self.check_source(tested, current, "build-manifest.json\nsrc/player/PlayerComponent.cpp")
        report, tested_bytes = self.source_report(tested)
        current_sha = hashlib.sha256(json.dumps(current).encode()).hexdigest()
        report["buildManifestSha256"] = current_sha
        with patch.object(validation.subprocess, "check_output", return_value=tested_bytes), \
             patch.object(validation.subprocess, "run"), self.assertRaisesRegex(ValueError, "tested manifest bytes"):
            validation.check_source_evidence(report, current, current_sha)

    def test_result_transition_cannot_change_dependencies_or_prelocked_evidence(self):
        tested = self.prepared_manifest()
        for field, value in (("releaseEvidenceUrl", "https://qt.io/changed"), ("fixEvidenceUrl", "https://qt.io/changed"),
                             ("memoryReportPath", "quality/changed.json"), ("memoryProtocolPath", "quality/changed-protocol.json"),
                             ("releaseProofPath", "quality/changed-proof.json"), ("fixRelease", "6.11.999")):
            current = copy.deepcopy(tested)
            current["qt"]["qualification"].update(qualified=True, **{field: value})
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "changed dependencies or release evidence"):
                self.check_source(tested, current)
        current = copy.deepcopy(tested)
        current["qt"]["qualification"]["qualified"] = True
        current["qt"]["archives"][0]["sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "changed dependencies or release evidence"):
            self.check_source(tested, current)

    def test_result_transition_requires_fix_proof_and_paths_before_measuring(self):
        for field in ("releaseEvidenceUrl", "fixEvidenceUrl", "memoryReportPath", "memoryProtocolPath", "releaseProofPath", "fixRelease"):
            tested = self.prepared_manifest()
            tested["qt"]["qualification"][field] = ""
            current = copy.deepcopy(tested)
            current["qt"]["qualification"]["qualified"] = True
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check_source(tested, current)

    def test_version_promotion_and_result_transition_preserve_all_other_fields(self):
        tested = self.prepared_manifest()
        stable = copy.deepcopy(tested)
        stable.update(appVersion="2.0.0", packageVersion="2.0.2")
        stable["qt"]["qualification"]["qualified"] = True
        self.check_source(tested, stable)
        stable["tools"]["vulkanRuntime"]["sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "changed qualified dependencies"):
            self.check_source(tested, stable)

    def memory_fixture(self):
        protocol = {"schemaVersion": 1, "scope": "windows-owned-process-tree-rss",
                    "requiredScenarios": ["navigation-svg", "long-playback"], "minimumDurationSeconds": 3600,
                    "warmupSeconds": 120, "sampleIntervalSeconds": 5,
                    "absoluteRssLimitBytes": 2147483648, "finalGrowthLimitBytes": 134217728}
        # In-memory synthetic unit data, never written as qualification evidence.
        raw = {"schemaVersion": 1, "scope": protocol["scope"], "scenarios": [
            {"name": name, "samples": [
                {"elapsedSeconds": seconds, "rssBytes": 100000000 if seconds == 0 else 150000000 if seconds == 1800 else 110000000,
                 "processCount": 4 if seconds == 1800 else 3}
                for seconds in range(0, 3601, 5)]}
            for name in protocol["requiredScenarios"]]}
        manifest = self.prepared_manifest()
        memory, _ = self.source_report(manifest)
        memory.update(issue="QTBUG-141377", passed=True, processTreeIncluded=True,
                      qualificationEligible=True, forcedGarbageCollection=False, scope=protocol["scope"],
                      workloads=[
                          {"schemaVersion": 1, "scenario": "navigation-svg", "initialized": True, "passed": True,
                           "scope": "hidden-full-desktop-webengine-svg", "forcedGarbageCollection": False,
                           "elapsedSeconds": 3720, "operationCount": 2976, "dataRequests": 26040,
                           "svgRequests": 20832, "navigationCount": 55},
                          {"schemaVersion": 1, "scenario": "long-playback", "initialized": True, "passed": True,
                           "scope": "hidden-full-desktop-webengine-and-native-music", "forcedGarbageCollection": False,
                           "elapsedSeconds": 3720, "operationCount": 2976, "positionChecks": 2976, "fileChanges": 55}],
                      rawSamplesPath="quality/test-only-raw.json", scenarios=[
                          {"name": name, "durationSeconds": 3600, "rssBaselineBytes": 100000000,
                           "rssPeakBytes": 150000000, "rssFinalBytes": 110000000, "limitBytes": protocol["absoluteRssLimitBytes"]}
                          for name in protocol["requiredScenarios"]])
        return manifest, memory, protocol, raw

    def check_memory(self, manifest, memory, protocol, raw, tested_protocol=None, preserve_hashes=False):
        protocol_bytes = (json.dumps(protocol) + "\n").encode()
        raw_bytes = (json.dumps(raw) + "\n").encode()
        if not preserve_hashes:
            memory["protocolSha256"] = hashlib.sha256(protocol_bytes).hexdigest()
            memory["rawSamplesSha256"] = hashlib.sha256(raw_bytes).hexdigest()
        files = {manifest["qt"]["qualification"]["memoryProtocolPath"]: protocol_bytes, memory["rawSamplesPath"]: raw_bytes}
        with patch.object(validation, "committed_bytes", side_effect=lambda path: files[path]), \
             patch.object(validation.subprocess, "check_output", return_value=tested_protocol or protocol_bytes):
            validation.check_memory_evidence(memory, manifest)

    def test_memory_gate_derives_summaries_from_committed_tree_samples(self):
        self.check_memory(*self.memory_fixture())
        for field in ("durationSeconds", "rssBaselineBytes", "rssPeakBytes", "rssFinalBytes", "limitBytes"):
            fixture = self.memory_fixture()
            fixture[1]["scenarios"][0][field] += 1
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "summary differs"):
                self.check_memory(*fixture)
        fixture = self.memory_fixture()
        fixture[1]["passed"] = False
        with self.assertRaisesRegex(ValueError, "passing Qt memory"):
            self.check_memory(*fixture)
        fixture[1]["passed"] = True
        fixture[1]["processTreeIncluded"] = False
        with self.assertRaisesRegex(ValueError, "process tree"):
            self.check_memory(*fixture)

    def test_qualification_flag_alone_cannot_publish(self):
        manifest, memory, _, _ = self.memory_fixture()
        manifest["qt"]["qualification"]["qualified"] = True
        memory["passed"] = False
        with patch.object(validation, "git", return_value=""), patch.object(validation, "committed_json", return_value=memory), \
             patch.object(validation, "check_source_evidence"), patch.object(validation, "check_release_proof"), \
             self.assertRaisesRegex(ValueError, "passing Qt memory"):
            validation.check_release(manifest, "v" + manifest["appVersion"])

    def release_proof_fixture(self):
        manifest = self.prepared_manifest()
        proof = {"releaseEvidenceUrl": manifest["qt"]["qualification"]["releaseEvidenceUrl"],
                 "archiveSha1s": {archive["name"]: archive["officialSha1"] for archive in manifest["qt"]["archives"]},
                 "observedUtc": "2020-01-02T00:00:00Z", "metadata": {
                     "name": manifest["qt"]["version"], "released": True, "releaseDate": "2020-01-01"}}
        return manifest, proof

    def test_offline_release_proof_requires_released_metadata_and_exact_url(self):
        manifest, proof = self.release_proof_fixture()
        with patch.object(validation, "committed_json", return_value=proof), patch.object(validation.urllib.request, "urlopen") as network:
            validation.check_release_proof(manifest)
            network.assert_not_called()
            for field, value in (("released", False), ("released", 1), ("name", "different-version"), ("releaseDate", "2099-01-01")):
                invalid = copy.deepcopy(proof)
                invalid["metadata"][field] = value
                with patch.object(validation, "committed_json", return_value=invalid), self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    validation.check_release_proof(manifest)
        for field, value in (("releaseEvidenceUrl", "https://qt.io/changed"), ("observedUtc", "2020-01-02T00:00:00"),
                             ("observedUtc", "2099-01-02T00:00:00Z")):
            invalid = copy.deepcopy(proof)
            invalid[field] = value
            with patch.object(validation, "committed_json", return_value=invalid), self.subTest(field=field), self.assertRaises(ValueError):
                validation.check_release_proof(manifest)

    def test_tag_release_proof_rechecks_live_jira_with_timeout_and_fails_closed(self):
        manifest, proof = self.release_proof_fixture()
        response = MagicMock()
        response.__enter__.return_value = response
        response.geturl.return_value = proof["releaseEvidenceUrl"]
        response.read.return_value = json.dumps(proof["metadata"]).encode()
        with patch.object(validation, "committed_json", return_value=proof), \
             patch.object(validation.urllib.request, "urlopen", return_value=response) as network, \
             patch.object(validation, "check_live_qt_archive_sha1s") as archives:
            validation.check_release_proof(manifest, verify_live=True)
            archives.assert_called_once_with(manifest)
            self.assertEqual(network.call_args.kwargs["timeout"], 10)
            self.assertEqual(network.call_args.args[0].full_url, proof["releaseEvidenceUrl"])
            response.read.return_value = json.dumps({**proof["metadata"], "released": False}).encode()
            with self.assertRaisesRegex(ValueError, "mark the locked version as released"):
                validation.check_release_proof(manifest, verify_live=True)
            response.read.return_value = b"not-json"
            with self.assertRaises(ValueError):
                validation.check_release_proof(manifest, verify_live=True)
            network.side_effect = OSError("test-only offline error")
            with self.assertRaises(OSError):
                validation.check_release_proof(manifest, verify_live=True)

    def test_offline_release_proof_requires_all_locked_archive_identities(self):
        manifest, proof = self.release_proof_fixture()
        for kind in ("missing", "changed", "extra", "absent"):
            invalid = copy.deepcopy(proof)
            first = manifest["qt"]["archives"][0]["name"]
            if kind == "missing":
                del invalid["archiveSha1s"][first]
            elif kind == "changed":
                invalid["archiveSha1s"][first] = "0" * 40
            elif kind == "extra":
                invalid["archiveSha1s"]["unreviewed.7z"] = "0" * 40
            else:
                del invalid["archiveSha1s"]
            with patch.object(validation, "committed_json", return_value=invalid), self.subTest(kind=kind), \
                 self.assertRaisesRegex(ValueError, "archive SHA1 identities"):
                validation.check_release_proof(manifest)

    def mocked_sidecar_opener(self, manifest, payload=None, final_url=None):
        by_url = {archive["url"] + ".sha1": archive for archive in manifest["qt"]["archives"]}
        opener = MagicMock()

        def open_sidecar(request, timeout):
            archive = by_url[request.full_url]
            response = MagicMock()
            response.__enter__.return_value = response
            response.geturl.return_value = final_url or request.full_url
            response.read.return_value = payload if payload is not None else (archive["officialSha1"] + "\n").encode()
            return response

        opener.open.side_effect = open_sidecar
        return opener

    def test_tag_gate_rechecks_every_archive_with_bounded_requests(self):
        manifest, proof = self.release_proof_fixture()
        opener = self.mocked_sidecar_opener(manifest)
        executor = validation.concurrent.futures.ThreadPoolExecutor
        with patch.object(validation.urllib.request, "build_opener", return_value=opener), \
             patch.object(validation.concurrent.futures, "ThreadPoolExecutor", wraps=executor) as workers:
            observed = validation.check_live_qt_archive_sha1s(manifest)
        self.assertEqual(observed, proof["archiveSha1s"])
        self.assertEqual(opener.open.call_count, len(manifest["qt"]["archives"]))
        self.assertEqual({call.args[0].full_url for call in opener.open.call_args_list},
                         {archive["url"] + ".sha1" for archive in manifest["qt"]["archives"]})
        self.assertTrue(all(call.kwargs["timeout"] == 10 for call in opener.open.call_args_list))
        workers.assert_called_once_with(max_workers=4)

    def test_tag_gate_rejects_changed_archive_or_malformed_official_checksum(self):
        manifest, _ = self.release_proof_fixture()
        opener = self.mocked_sidecar_opener(manifest)
        original = opener.open.side_effect
        last_url = manifest["qt"]["archives"][-1]["url"] + ".sha1"

        def last_archive_changed(request, timeout):
            response = original(request, timeout)
            if request.full_url == last_url:
                response.read.return_value = b"0" * 40
            return response

        opener.open.side_effect = last_archive_changed
        with patch.object(validation.urllib.request, "build_opener", return_value=opener), \
             self.assertRaisesRegex(ValueError, "differs from the tested package"):
            validation.check_live_qt_archive_sha1s(manifest)
        for payload in (b"0" * 40, b"", b"g" * 40, b"a" * 39, b"a" * 41,
                        b"a" * 40 + b" archive.7z", b"a" * 65):
            opener = self.mocked_sidecar_opener(manifest, payload=payload)
            with patch.object(validation.urllib.request, "build_opener", return_value=opener), \
                 self.subTest(payload=payload), self.assertRaises(ValueError):
                validation.check_live_qt_archive_sha1s(manifest)

    def test_tag_gate_fails_closed_on_untrusted_redirect_and_offline_sidecar(self):
        manifest, _ = self.release_proof_fixture()
        opener = self.mocked_sidecar_opener(manifest, final_url="https://unreviewed.example/archive.7z.sha1")
        with patch.object(validation.urllib.request, "build_opener", return_value=opener), \
             self.assertRaisesRegex(ValueError, "redirected"):
            validation.check_live_qt_archive_sha1s(manifest)
        opener.open.side_effect = OSError("test-only unavailable sidecar")
        with patch.object(validation.urllib.request, "build_opener", return_value=opener), self.assertRaises(OSError):
            validation.check_live_qt_archive_sha1s(manifest)
        handler = validation.OfficialQtSidecarRedirect()
        request = validation.urllib.request.Request(manifest["qt"]["archives"][0]["url"] + ".sha1")
        for url in ("http://download.qt.io/archive.sha1", "https://download.qt.io.evil.example/archive.sha1",
                    "https://mirror.example/archive.sha1", "https://download.qt.io/archive.sha1?changed=1"):
            with self.subTest(url=url), self.assertRaisesRegex(ValueError, "redirected"):
                handler.redirect_request(request, None, 302, "Found", {}, url)

    def test_release_metadata_rejects_unofficial_endpoints_and_redirects(self):
        manifest, proof = self.release_proof_fixture()
        for url in ("http://qt-project.atlassian.net/rest/api/2/version/16945",
                    "https://qt-project.atlassian.net.evil.example/rest/api/2/version/16945",
                    "https://qt-project.atlassian.net/rest/api/2/version/16945?changed=1"):
            manifest["qt"]["qualification"]["releaseEvidenceUrl"] = url
            with self.subTest(url=url), self.assertRaisesRegex(ValueError, "official Jira"):
                validation.check_release_proof(manifest)
        manifest, proof = self.release_proof_fixture()
        response = MagicMock()
        response.__enter__.return_value = response
        response.geturl.return_value = "https://example.invalid/version/16945"
        with patch.object(validation, "committed_json", return_value=proof), \
             patch.object(validation.urllib.request, "urlopen", return_value=response), self.assertRaisesRegex(ValueError, "redirected"):
            validation.check_release_proof(manifest, verify_live=True)

    def test_evidence_paths_cannot_escape_checkout_on_windows(self):
        for path in ("", "/quality/report.json", "../report.json", "C:/quality/report.json", "quality\\report.json"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                validation.evidence_path(path)

    def test_memory_gate_binds_protocol_to_tested_commit_and_raw_byte_hashes(self):
        fixture = self.memory_fixture()
        original = (json.dumps(fixture[2]) + "\n").encode()
        fixture[2]["absoluteRssLimitBytes"] *= 2
        with self.assertRaisesRegex(ValueError, "protocol changed"):
            self.check_memory(*fixture, tested_protocol=original)
        for field in ("protocolSha256", "rawSamplesSha256"):
            fixture = self.memory_fixture()
            self.check_memory(*fixture)
            fixture[1][field] = "0" * 64
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "protocol bytes|SHA256"):
                self.check_memory(*fixture, preserve_hashes=True)

    def test_memory_gate_requires_distinct_scenarios_hour_and_finite_tree_samples(self):
        for container in (1, 3):
            fixture = self.memory_fixture()
            fixture[container]["scenarios"][1]["name"] = "navigation-svg"
            with self.subTest(container=container), self.assertRaisesRegex(ValueError, "both navigation"):
                self.check_memory(*fixture)
        for field, value in (("elapsedSeconds", 1800), ("elapsedSeconds", float("nan")),
                             ("rssBytes", 0), ("rssBytes", float("inf")), ("processCount", True)):
            fixture = self.memory_fixture()
            fixture[3]["scenarios"][0]["samples"][-1][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.check_memory(*fixture)
        fixture = self.memory_fixture()
        fixture[3]["scenarios"][0]["samples"][-1]["elapsedSeconds"] = 3599
        with self.assertRaisesRegex(ValueError, "at least one hour"):
            self.check_memory(*fixture)
        fixture[3]["scope"] = "main-process-only"
        with self.assertRaisesRegex(ValueError, "process scope"):
            self.check_memory(*fixture)

    def test_memory_gate_rejects_late_baseline_missing_intervals_and_two_point_reports(self):
        for mutation in ("late-baseline", "missed-intervals", "two-points"):
            fixture = self.memory_fixture()
            samples = fixture[3]["scenarios"][0]["samples"]
            if mutation == "late-baseline":
                samples[0]["elapsedSeconds"] = 1
            elif mutation == "missed-intervals":
                del samples[1:3]
            else:
                fixture[3]["scenarios"][0]["samples"] = [samples[0], samples[-1]]
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, "elapsed zero|sample cadence|sample count"):
                self.check_memory(*fixture)

    def test_memory_gate_rejects_smoke_gc_or_wrong_report_scope(self):
        for field, value in (("qualificationEligible", False), ("qualificationEligible", 1),
                             ("forcedGarbageCollection", True), ("forcedGarbageCollection", 0),
                             ("scope", "main-process-only")):
            fixture = self.memory_fixture()
            fixture[1][field] = value
            with self.subTest(field=field, value=value), self.assertRaisesRegex(ValueError, "eligible|garbage collection|process scope"):
                self.check_memory(*fixture)
        for field in ("qualificationEligible", "forcedGarbageCollection", "scope"):
            fixture = self.memory_fixture()
            del fixture[1][field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                self.check_memory(*fixture)

    def test_memory_gate_requires_two_distinct_successful_non_gc_workloads(self):
        for mutation in ("absent", "empty", "one", "extra", "duplicate", "unknown"):
            fixture = self.memory_fixture()
            workloads = fixture[1]["workloads"]
            if mutation == "absent":
                del fixture[1]["workloads"]
            elif mutation == "empty":
                fixture[1]["workloads"] = []
            elif mutation == "one":
                workloads.pop()
            elif mutation == "extra":
                workloads.append(copy.deepcopy(workloads[0]))
            elif mutation == "duplicate":
                workloads[1]["scenario"] = workloads[0]["scenario"]
            else:
                workloads[1]["scenario"] = "unrelated-page"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.check_memory(*fixture)
        for index in (0, 1):
            for field, value in (("initialized", False), ("initialized", 1), ("passed", False), ("passed", 1),
                                 ("forcedGarbageCollection", True), ("forcedGarbageCollection", 0),
                                 ("scope", "unrelated-page"), ("elapsedSeconds", 3719),
                                 ("elapsedSeconds", float("nan"))):
                fixture = self.memory_fixture()
                fixture[1]["workloads"][index][field] = value
                with self.subTest(index=index, field=field, value=value), self.assertRaises(ValueError):
                    self.check_memory(*fixture)

    def test_memory_gate_requires_real_svg_navigation_and_native_playback_cadence(self):
        for index, fields in ((0, ("operationCount", "dataRequests", "svgRequests", "navigationCount")),
                              (1, ("operationCount", "positionChecks", "fileChanges"))):
            for field in fields:
                for mutation in ("below-floor", "missing", "bool", "fractional"):
                    fixture = self.memory_fixture()
                    workload = fixture[1]["workloads"][index]
                    if mutation == "below-floor":
                        workload[field] -= 1
                    elif mutation == "missing":
                        del workload[field]
                    elif mutation == "bool":
                        workload[field] = True
                    else:
                        workload[field] = float(workload[field])
                    with self.subTest(index=index, field=field, mutation=mutation), self.assertRaisesRegex(ValueError, "cadence is insufficient"):
                        self.check_memory(*fixture)
        fixture = self.memory_fixture()
        # A reported workload cannot stop at the nominal minimum if actual
        # RSS observation continued beyond it. Keep raw/summary identities valid.
        for scenario in fixture[3]["scenarios"]:
            scenario["samples"][-1]["elapsedSeconds"] = 3605
        for scenario in fixture[1]["scenarios"]:
            scenario["durationSeconds"] = 3605
        with self.assertRaisesRegex(ValueError, "warmup and entire measured interval"):
            self.check_memory(*fixture)

    def test_memory_gate_rejects_a_one_process_tree_sample(self):
        for scenario in (0, 1):
            for count in (1, 0, True, 2.0):
                fixture = self.memory_fixture()
                fixture[3]["scenarios"][scenario]["samples"][300]["processCount"] = count
                with self.subTest(scenario=scenario, count=count), self.assertRaisesRegex(ValueError, "native/WebEngine process tree"):
                    self.check_memory(*fixture)

    def test_memory_gate_checks_predeclared_peak_and_final_growth_limits(self):
        fixture = self.memory_fixture()
        fixture[2]["absoluteRssLimitBytes"] = 120000000
        for summary in fixture[1]["scenarios"]:
            summary["limitBytes"] = 120000000
        with self.assertRaisesRegex(ValueError, "predeclared RSS"):
            self.check_memory(*fixture)
        fixture = self.memory_fixture()
        fixture[2]["finalGrowthLimitBytes"] = 5000000
        with self.assertRaisesRegex(ValueError, "final-growth"):
            self.check_memory(*fixture)

    def test_final_handoff_and_synthetic_fixtures_keep_honest_provenance(self):
        validation.check_final_contract(CONTRACT, FIXTURE)
        for field, value in (("contractRevision", "old-revision"), ("serverCommit", "0" * 40), ("capturedFromLiveServer", True)):
            fixture = copy.deepcopy(FIXTURE)
            fixture[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validation.check_final_contract(CONTRACT, fixture)
        contract = copy.deepcopy(CONTRACT)
        contract["evidence"]["finalServerAgentHandoffReceived"] = False
        with self.assertRaisesRegex(ValueError, "handoff has not been verified"):
            validation.check_final_contract(contract, FIXTURE)
        # A separately source-reviewed contract remains a valid fallback; it
        # must not claim final handoff receipt that never happened.
        contract["evidence"]["kind"] = "immutable-deployed-source-and-source-derived-synthetic-fixtures"
        validation.check_final_contract(contract, FIXTURE)

    def native_acceptance_fixture(self):
        return {"accepted": True, "acceptedBy": "user", "nativeAcceptancePerformed": True,
                "apiRevision": CONTRACT["apiRevision"], "serverCommit": CONTRACT["serverCommit"], "webCommit": CONTRACT["webCommit"],
                "syncPlay": {"composition": "windows-desktop-and-web", "correctionEnabled": True, "coldSecondClient": True,
                    "mixedClientCases": ["pause-seek", "rejoin-reconnect", "legacy-409", "personal-speed-restoration", "independent-audio-subtitles", "direct-play", "hls"],
                    "driftSamples": [{"baseRate": rate, "afterSeconds": seconds, "absoluteDriftMilliseconds": 12}
                        for rate in CONTRACT["syncPlay"]["driftAcceptance"]["baseRates"]
                        for seconds in CONTRACT["syncPlay"]["driftAcceptance"]["sampleAfterSeconds"]]}}

    def test_browser_receipt_or_different_contract_cannot_be_native_acceptance(self):
        acceptance = self.native_acceptance_fixture()
        validation.check_native_acceptance(acceptance, CONTRACT)
        acceptance["nativeAcceptancePerformed"] = False
        with self.assertRaisesRegex(ValueError, "Browser acceptance"):
            validation.check_native_acceptance(acceptance, CONTRACT)
        acceptance = self.native_acceptance_fixture()
        acceptance["apiRevision"] = "another-revision"
        with self.assertRaisesRegex(ValueError, "final API"):
            validation.check_native_acceptance(acceptance, CONTRACT)

    def test_mixed_client_drift_must_cover_every_contract_case_below_limit(self):
        acceptance = self.native_acceptance_fixture()
        acceptance["syncPlay"]["driftSamples"].pop()
        with self.assertRaisesRegex(ValueError, "rate/time observation"):
            validation.check_native_acceptance(acceptance, CONTRACT)
        for invalid_drift in (CONTRACT["syncPlay"]["driftAcceptance"]["absoluteDriftMillisecondsExclusiveMaximum"], float("nan"), -1, True):
            acceptance = self.native_acceptance_fixture()
            acceptance["syncPlay"]["driftSamples"][0]["absoluteDriftMilliseconds"] = invalid_drift
            with self.subTest(drift=invalid_drift), self.assertRaises(ValueError):
                validation.check_native_acceptance(acceptance, CONTRACT)
        acceptance = self.native_acceptance_fixture()
        acceptance["syncPlay"]["mixedClientCases"].remove("legacy-409")
        with self.assertRaisesRegex(ValueError, "mixed-client case"):
            validation.check_native_acceptance(acceptance, CONTRACT)


if __name__ == "__main__":
    unittest.main()
