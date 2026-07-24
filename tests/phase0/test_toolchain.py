import unittest

from scripts.phase0.check_toolchain import validate_lock


class ToolchainTest(unittest.TestCase):
    def test_exact_phase0_lock(self) -> None:
        lock = {
            "esp_idf": {
                "tag": "v5.5.4",
                "commit": "735507283d5b2f9fb363a1901172dbd9e847945d",
            },
            "node": {
                "version": "22.14.0",
                "sha256": "e9404633bc02a5162c5c573b1e2490f5fb44648345d64a958b17e325729a5e42",
            },
            "python": {"version": "3.11.11"},
        }
        self.assertEqual([], validate_lock(lock))

    def test_moved_idf_tag_is_rejected(self) -> None:
        lock = {
            "esp_idf": {
                "tag": "v5.5.4",
                "commit": "wrong",
            },
            "node": {
                "version": "22.14.0",
                "sha256": "wrong",
            },
            "python": {"version": "3.11.11"},
        }
        self.assertIn("esp_idf.commit", validate_lock(lock))
