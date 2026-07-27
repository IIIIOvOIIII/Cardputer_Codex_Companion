from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[3]
AUDITOR_PATH = ROOT / "tools/product/audit_public_release.py"


def load_auditor():
    spec = importlib.util.spec_from_file_location(
        "audit_public_release", AUDITOR_PATH
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def auditor():
    return load_auditor()


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def initialize_repo(path: Path) -> None:
    path.mkdir()
    git(path, "init", "-b", "main")
    git(path, "config", "user.name", "Public Release Test")
    git(path, "config", "user.email", "release-test@example.invalid")
    (path / "README.md").write_text("public fixture\n", encoding="utf-8")
    git(path, "add", "README.md")
    git(path, "commit", "-m", "initial")


def commit_file(repo: Path, relative: str, content: str) -> str:
    target = repo / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")
    git(repo, "add", relative)
    git(repo, "commit", "-m", f"add {relative}")
    return git(repo, "rev-parse", "HEAD")


def rule_ids(result) -> set[str]:
    return {finding.rule_id for finding in result.findings}


def test_detects_secret_path_on_non_current_branch(
    tmp_path: Path, auditor
):
    repo = tmp_path / "repo"
    initialize_repo(repo)
    git(repo, "switch", "-c", "private-history")
    commit_file(repo, ".env", "WIFI_PASSWORD=not-a-placeholder\n")
    git(repo, "switch", "main")

    result = auditor.audit_repository(repo)

    assert "credential_path" in rule_ids(result)
    assert any(finding.path == ".env" for finding in result.findings)


def test_detects_secret_reachable_only_from_reflog(
    tmp_path: Path, auditor
):
    repo = tmp_path / "repo"
    initialize_repo(repo)
    secret = "ghp_" + "A" * 36
    commit_file(repo, "notes.txt", f"token={secret}\n")
    git(repo, "reset", "--hard", "HEAD^")

    result = auditor.audit_repository(repo)

    assert "github_token" in rule_ids(result)


def test_detects_retained_unreachable_secret_blob(
    tmp_path: Path, auditor
):
    repo = tmp_path / "repo"
    initialize_repo(repo)
    secret = (
        "-----BEGIN PRIVATE KEY-----\n"
        + "A" * 96
        + "\n-----END PRIVATE KEY-----\n"
    )
    commit_file(repo, "temporary.txt", secret)
    git(repo, "reset", "--hard", "HEAD^")
    git(repo, "reflog", "expire", "--expire=now", "--all")

    result = auditor.audit_repository(repo)

    assert result.unreachable_objects > 0
    assert "private_key" in rule_ids(result)


@pytest.mark.parametrize(
    "relative",
    (
        "build/private/wifi_cfg.bin",
        "dist/private/cardputer_codex_companion-private-full.bin",
    ),
)
def test_rejects_private_artifact_paths(
    tmp_path: Path, auditor, relative: str
):
    repo = tmp_path / "repo"
    initialize_repo(repo)
    artifacts = tmp_path / "dist"
    target = artifacts / relative
    target.parent.mkdir(parents=True)
    target.write_bytes(b"private fixture")

    result = auditor.audit_artifacts(artifacts)

    assert "private_artifact" in rule_ids(result)
    assert any(finding.path == relative for finding in result.findings)


def test_reports_findings_without_printing_candidate_values(
    tmp_path: Path
):
    repo = tmp_path / "repo"
    initialize_repo(repo)
    secret = "ghp_" + "Z" * 36
    commit_file(repo, "leak.txt", f"token={secret}\n")
    report = tmp_path / "report.md"

    result = subprocess.run(
        [
            sys.executable,
            str(AUDITOR_PATH),
            "--repo",
            str(repo),
            "--report",
            str(report),
        ],
        capture_output=True,
        text=True,
    )

    assert result.returncode == 1
    output = result.stdout + result.stderr + report.read_text(encoding="utf-8")
    assert secret not in output
    assert "github_token" in output


def test_safe_test_fixtures_and_clean_artifacts_pass(
    tmp_path: Path, auditor
):
    repo = tmp_path / "repo"
    initialize_repo(repo)
    commit_file(
        repo,
        "tests/fixtures/pairing.json",
        json.dumps(
            {
                "pin": "12345678",
                "password": "example-password",
                "private_key_marker": "-----BEGIN PRIVATE KEY-----",
                "url": "https://user:password@example.invalid/test-only",
            }
        ),
    )
    artifacts = tmp_path / "dist"
    artifacts.mkdir()
    (artifacts / "cardputer_codex_companion-full.bin").write_bytes(
        b"\xff" * 512
    )

    repository_result = auditor.audit_repository(repo)
    artifact_result = auditor.audit_artifacts(artifacts)

    assert repository_result.findings == []
    assert artifact_result.findings == []


def test_public_tree_has_no_private_packaging_entry_points(auditor):
    result = auditor.audit_current_tree(ROOT)

    assert result.findings == []
    assert not (ROOT / "scripts/package_private_firmware.sh").exists()
    assert not (ROOT / "tools/product/generate_wifi_nvs.py").exists()
