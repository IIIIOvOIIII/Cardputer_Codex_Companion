#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import Iterable


MAX_SCANNED_BLOB_BYTES = 16 * 1024 * 1024
FORBIDDEN_CURRENT_ENTRY_POINTS = frozenset(
    {
        "scripts/package_private_firmware.sh",
        "tools/product/generate_wifi_nvs.py",
        "tools/product/tests/test_private_packaging.py",
    }
)

PRIVATE_KEY_PATTERN = re.compile(
    rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"
    rb"\s+[A-Za-z0-9+/=\r\n]{64,}"
    rb"-----END (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"
)
GITHUB_TOKEN_PATTERN = re.compile(rb"\bgh[opusr]_[A-Za-z0-9]{36,255}\b")
AWS_ACCESS_KEY_PATTERN = re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")


@dataclass(frozen=True, order=True)
class Finding:
    rule_id: str
    location: str
    path: str = "-"


@dataclass
class AuditResult:
    findings: list[Finding] = field(default_factory=list)
    refs: int = 0
    reflog_commits: int = 0
    unreachable_objects: int = 0
    blobs_scanned: int = 0
    files_scanned: int = 0

    def extend(self, other: "AuditResult") -> None:
        self.findings.extend(other.findings)
        self.refs += other.refs
        self.reflog_commits += other.reflog_commits
        self.unreachable_objects += other.unreachable_objects
        self.blobs_scanned += other.blobs_scanned
        self.files_scanned += other.files_scanned

    def normalize(self) -> "AuditResult":
        self.findings = sorted(set(self.findings))
        return self


def _git(
    repo: Path,
    *arguments: str,
    check: bool = True,
    text: bool = True,
) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=check,
        capture_output=True,
        text=text,
    )


def _historical_paths(repo: Path) -> set[str]:
    result = _git(
        repo,
        "log",
        "--all",
        "--reflog",
        "--format=",
        "--name-only",
        "--diff-filter=ACMR",
    )
    return {
        line.strip()
        for line in result.stdout.splitlines()
        if line.strip()
    }


def _object_paths(repo: Path) -> dict[str, str]:
    result = _git(
        repo,
        "rev-list",
        "--objects",
        "--all",
        "--reflog",
    )
    paths: dict[str, str] = {}
    for line in result.stdout.splitlines():
        object_id, separator, path = line.partition(" ")
        if separator and path:
            paths.setdefault(object_id, path)
    return paths


def _credential_path_rule(path: str) -> str | None:
    normalized = PurePosixPath(path)
    lower_name = normalized.name.lower()
    lower_path = path.lower()
    if lower_name == ".env" or (
        lower_name.startswith(".env.")
        and lower_name not in {".env.example", ".env.sample", ".env.template"}
    ):
        return "credential_path"
    if lower_name in {
        "credentials.json",
        "secrets.json",
        "wifi_cfg.bin",
    }:
        return "credential_path"
    if lower_name.endswith((".p12", ".pfx")):
        return "credential_path"
    if lower_name.endswith((".pem", ".key")) and not any(
        part.lower() in {"test", "tests", "fixtures"}
        for part in normalized.parts
    ):
        return "credential_path"
    if (
        lower_path.startswith("build/private/")
        or lower_path.startswith("dist/private/")
        or lower_name.endswith("-private-full.bin")
    ):
        return "private_artifact"
    return None


def _content_rules(content: bytes) -> Iterable[str]:
    if PRIVATE_KEY_PATTERN.search(content):
        yield "private_key"
    if GITHUB_TOKEN_PATTERN.search(content):
        yield "github_token"
    if AWS_ACCESS_KEY_PATTERN.search(content):
        yield "aws_access_key"


def _retained_unreachable_count(repo: Path) -> int:
    result = _git(
        repo,
        "fsck",
        "--full",
        "--unreachable",
        "--no-reflogs",
        check=False,
    )
    return sum(
        1
        for line in (result.stdout + result.stderr).splitlines()
        if line.startswith("unreachable ")
    )


def audit_repository(repo: Path) -> AuditResult:
    repo = repo.resolve()
    _git(repo, "rev-parse", "--git-dir")
    result = AuditResult()
    result.refs = len(
        [
            line
            for line in _git(repo, "for-each-ref", "--format=%(refname)").stdout
            .splitlines()
            if line
        ]
    )
    result.reflog_commits = len(
        {
            line
            for line in _git(repo, "rev-list", "--reflog", "--all").stdout
            .splitlines()
            if line
        }
    )
    result.unreachable_objects = _retained_unreachable_count(repo)

    for path in sorted(_historical_paths(repo)):
        rule = _credential_path_rule(path)
        if rule is not None:
            result.findings.append(
                Finding(rule_id=rule, location="history-path", path=path)
            )

    paths = _object_paths(repo)
    objects = _git(
        repo,
        "cat-file",
        "--batch-all-objects",
        "--batch-check=%(objectname) %(objecttype) %(objectsize)",
    )
    for line in objects.stdout.splitlines():
        fields = line.split()
        if len(fields) != 3 or fields[1] != "blob":
            continue
        object_id, _, raw_size = fields
        size = int(raw_size)
        if size > MAX_SCANNED_BLOB_BYTES:
            result.findings.append(
                Finding(
                    rule_id="oversized_blob",
                    location=f"object:{object_id}",
                    path=paths.get(object_id, "-"),
                )
            )
            continue
        content = _git(
            repo,
            "cat-file",
            "blob",
            object_id,
            text=False,
        ).stdout
        result.blobs_scanned += 1
        for rule in _content_rules(content):
            result.findings.append(
                Finding(
                    rule_id=rule,
                    location=f"object:{object_id}",
                    path=paths.get(object_id, "-"),
                )
            )
    return result.normalize()


def audit_current_tree(repo: Path) -> AuditResult:
    repo = repo.resolve()
    result = AuditResult()
    tracked = _git(repo, "ls-files").stdout.splitlines()
    for path in tracked:
        target = repo / path
        if not target.is_file():
            continue
        result.files_scanned += 1
        if path in FORBIDDEN_CURRENT_ENTRY_POINTS:
            result.findings.append(
                Finding(
                    rule_id="private_packaging_entry",
                    location="current-tree",
                    path=path,
                )
            )
        if target.stat().st_size <= MAX_SCANNED_BLOB_BYTES:
            for rule in _content_rules(target.read_bytes()):
                result.findings.append(
                    Finding(
                        rule_id=rule,
                        location="current-tree",
                        path=path,
                    )
                )
    return result.normalize()


def audit_artifacts(artifacts: Path) -> AuditResult:
    artifacts = artifacts.resolve()
    result = AuditResult()
    if not artifacts.exists():
        return result
    for path in sorted(artifacts.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(artifacts).as_posix()
        result.files_scanned += 1
        normalized = PurePosixPath(relative)
        lower_name = normalized.name.lower()
        if (
            any(part.lower() == "private" for part in normalized.parts)
            or lower_name == "wifi_cfg.bin"
            or lower_name.endswith("-private-full.bin")
        ):
            result.findings.append(
                Finding(
                    rule_id="private_artifact",
                    location="artifact",
                    path=relative,
                )
            )
            continue
        if path.stat().st_size <= MAX_SCANNED_BLOB_BYTES:
            content = path.read_bytes()
            for rule in _content_rules(content):
                result.findings.append(
                    Finding(
                        rule_id=rule,
                        location="artifact",
                        path=relative,
                    )
                )
    return result.normalize()


def _render_report(result: AuditResult) -> str:
    lines = [
        "# Public Release Security Audit",
        "",
        "The audit output intentionally excludes candidate secret values.",
        "",
        f"- Refs enumerated: {result.refs}",
        f"- Reflog commits enumerated: {result.reflog_commits}",
        f"- Retained unreachable objects: {result.unreachable_objects}",
        f"- Git blobs scanned: {result.blobs_scanned}",
        f"- Current/artifact files scanned: {result.files_scanned}",
        f"- Findings: {len(result.findings)}",
        "",
    ]
    if result.findings:
        lines.extend(
            [
                "## Findings",
                "",
                "| Rule | Location | Path |",
                "| --- | --- | --- |",
            ]
        )
        for finding in result.findings:
            path = finding.path.replace("|", "%7C")
            lines.append(
                f"| {finding.rule_id} | {finding.location} | {path} |"
            )
    else:
        lines.extend(
            [
                "## Result",
                "",
                "PASS: no credential or forbidden private-release artifact "
                "was detected.",
            ]
        )
    lines.append("")
    return "\n".join(lines)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(arguments)

    combined = AuditResult()
    combined.extend(audit_repository(args.repo))
    combined.extend(audit_current_tree(args.repo))
    if args.artifacts is not None:
        combined.extend(audit_artifacts(args.artifacts))
    combined.normalize()

    report = _render_report(combined)
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    sys.stdout.write(report)
    return 1 if combined.findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
