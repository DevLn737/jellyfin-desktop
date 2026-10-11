"""Release regressions: qualification blocks, invalid locks, and RC promotion evidence."""
import copy
import hashlib
import importlib.util
import json
import pathlib
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("release_validation", ROOT / "scripts/validate-build-manifest.py")
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)
MANIFEST = json.loads((ROOT / "build-manifest.json").read_text(encoding="utf-8-sig"))


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


if __name__ == "__main__":
    unittest.main()
