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
