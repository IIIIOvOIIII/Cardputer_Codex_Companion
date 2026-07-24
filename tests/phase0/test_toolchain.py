import hashlib
import json
import os
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path
from textwrap import dedent

from scripts.phase0.check_toolchain import validate_lock


class ToolchainTest(unittest.TestCase):
    repo_root = Path(__file__).resolve().parents[2]

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

    def test_sdkconfig_defaults_publish_keyboard_gap_identity(self) -> None:
        defaults = (self.repo_root / "firmware" / "sdkconfig.defaults").read_text(
            encoding="utf-8"
        )
        self.assertIn('CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="Cardputer Codex"', defaults)
        self.assertIn("CONFIG_BT_NIMBLE_SVC_GAP_APPEARANCE=961", defaults)
        self.assertIn("CONFIG_BT_NIMBLE_SM_LVL=2", defaults)


class BootstrapScriptTest(unittest.TestCase):
    bootstrap_script = Path(__file__).resolve().parents[2] / "scripts/phase0/bootstrap_toolchain.sh"
    idf_commit = "735507283d5b2f9fb363a1901172dbd9e847945d"
    node_archive = "node-v22.14.0-darwin-arm64.tar.gz"

    def _write_executable(self, path: Path, content: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(dedent(content), encoding="utf-8")
        path.chmod(0o755)

    def _run_bootstrap(
        self,
        repo_root: Path,
        tool_dir: Path,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{tool_dir}:{env['PATH']}",
                "CC_FAKE_REPO_ROOT": str(repo_root),
                "CC_TEST_IDF_COMMIT": self.idf_commit,
            }
        )
        return subprocess.run(
            [str(self.bootstrap_script)],
            cwd=str(repo_root),
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def _build_lock(self, repo_root: Path, python_version: str, node_sha: str) -> None:
        lock = {
            "esp_idf": {
                "tag": "v5.5.4",
                "commit": self.idf_commit,
            },
            "node": {
                "version": "22.14.0",
                "archive": self.node_archive,
                "sha256": node_sha,
            },
            "python": {
                "version": python_version,
            },
        }
        (repo_root / "toolchain.lock.json").write_text(json.dumps(lock), encoding="utf-8")

    def _fake_git(self, tool_dir: Path) -> None:
        self._write_executable(
            tool_dir / "git",
            """
            #!/usr/bin/env bash
            set -euo pipefail
            if [ "${1:-}" = "rev-parse" ]; then
              if [ "${2:-}" = "--show-toplevel" ]; then
                echo "${CC_FAKE_REPO_ROOT}"
                exit 0
              fi
              if [ "${2:-}" = "HEAD" ]; then
                echo "${CC_TEST_IDF_COMMIT}"
                exit 0
              fi
            fi
            if [ "${1:-}" = "clone" ]; then
              dest="$4"
              mkdir -p "${dest}/.git"
              exit 0
            fi
            exit 0
            """,
        )

    def _fake_uv(self, tool_dir: Path, python_version: str, call_log: Path | None = None) -> None:
        call_log_text = str(call_log) if call_log is not None else ""
        self._write_executable(
            tool_dir / "uv",
            f"""
            #!/usr/bin/env bash
            set -euo pipefail
            if [ "${{1:-}}" = "venv" ]; then
              if [ -n "{call_log_text}" ]; then
                printf '%s\\n' "$*" >> "{call_log_text}"
              fi
              venv_dir="$2"
              mkdir -p "${{venv_dir}}/bin"
              cat > "${{venv_dir}}/bin/python3" <<'PYEOF'
            #!/usr/bin/env bash
            echo "Python {python_version}"
            PYEOF
              chmod +x "${{venv_dir}}/bin/python3"
              ln -sf "${{venv_dir}}/bin/python3" "${{venv_dir}}/bin/python"
            fi
            """,
        )

    def _fake_install_script(self, esp_idf_dir: Path, repo_root: Path) -> None:
        self._write_executable(
            esp_idf_dir / "install.sh",
            f"""
            #!/usr/bin/env bash
            set -euo pipefail
            printf '%s\\n' "$PYTHON" > "{repo_root}/install_python_env.txt"
            command -v python3 > "{repo_root}/install_python_which.txt" || true
            printf '%s\\n' "$PATH" > "{repo_root}/install_path.txt"
            printf '%s\\n' "$*" > "{repo_root}/install_args.txt"
            python3 --version > "{repo_root}/install_python_version.txt" 2>&1 || true
            touch "{repo_root}/install_called.txt"
            """,
        )

    def _make_node_archive(self, tools_dir: Path) -> str:
        tar_path = tools_dir / self.node_archive
        payload = tools_dir / "node_payload.txt"
        payload.write_text("node payload", encoding="utf-8")
        with tarfile.open(tar_path, "w:gz") as tar:
            tar.add(payload, arcname="node_payload.txt")
        return hashlib.sha256(tar_path.read_bytes()).hexdigest()

    def test_bootstrap_rejects_repo_local_python_version_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            repo_root = Path(td) / "project mismatch"
            repo_root.mkdir()
            tools_dir = repo_root / ".tools"
            tools_dir.mkdir(parents=True)
            (tools_dir / "uv-python" / "bin").mkdir(parents=True)
            (tools_dir / "esp-idf" / ".git").mkdir(parents=True)

            mismatch_python = tools_dir / "uv-python" / "bin" / "python3"
            mismatch_python.write_text("#!/usr/bin/env bash\necho 'Python 3.14.3'\n", encoding="utf-8")
            mismatch_python.chmod(0o755)

            archive_sha = self._make_node_archive(tools_dir)
            self._build_lock(repo_root, python_version="3.11.11", node_sha=archive_sha)

            tool_dir = repo_root / "fake-bin"
            uv_call_log = tools_dir / "uv_call.log"
            self._fake_git(tool_dir)
            self._fake_uv(tool_dir, "3.14.3", uv_call_log)
            self._fake_install_script(tools_dir / "esp-idf", repo_root)

            result = self._run_bootstrap(repo_root, tool_dir)
            self.assertNotEqual(result.returncode, 0, msg=result.stdout + result.stderr)
            self.assertIn("uv-python version mismatch: expected 3.11.11, got 3.14.3", result.stderr)
            self.assertFalse(uv_call_log.exists())
            self.assertFalse((repo_root / "install_called.txt").exists())
            self.assertEqual(mismatch_python.read_text(), "#!/usr/bin/env bash\necho 'Python 3.14.3'\n")

    def test_bootstrap_uses_repo_local_python_for_install(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            repo_root = Path(td) / "project success"
            repo_root.mkdir()
            tools_dir = repo_root / ".tools"
            tools_dir.mkdir(parents=True)
            (tools_dir / "esp-idf" / ".git").mkdir(parents=True)

            archive_sha = self._make_node_archive(tools_dir)
            self._build_lock(repo_root, python_version="3.11.11", node_sha=archive_sha)

            tool_dir = repo_root / "fake-bin"
            uv_call_log = tools_dir / "uv_call.log"
            self._fake_uv(tool_dir, "3.11.11", uv_call_log)
            self._fake_git(tool_dir)
            self._fake_install_script(tools_dir / "esp-idf", repo_root)

            result = self._run_bootstrap(repo_root, tool_dir)
            self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
            self.assertTrue((repo_root / "install_called.txt").exists())

            install_which = (repo_root / "install_python_which.txt").read_text()
            install_env = (repo_root / "install_python_env.txt").read_text()
            self.assertIn(str((repo_root / ".tools" / "uv-python" / "bin" / "python3")), install_which)
            self.assertIn(str((repo_root / ".tools" / "uv-python" / "bin" / "python3")), install_env)
            self.assertIn("esp32s3", (repo_root / "install_args.txt").read_text())
            self.assertIn("3.11.11", (repo_root / "install_python_version.txt").read_text())
            self.assertIn("venv", uv_call_log.read_text())


if __name__ == "__main__":
    unittest.main()
