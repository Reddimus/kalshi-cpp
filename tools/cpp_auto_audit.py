#!/usr/bin/env python3
"""Audit repo-owned C++ sources for disallowed local ``auto`` usage.

Policy:
- allow ``auto`` for structured bindings
- allow ``auto`` for lambda closure objects
- allow ``auto`` for iterator-like values
- allow explicit one-off exceptions marked with ``auto-ok``

Everything else is expected to spell out the type directly. Repos can
carry a local allowlist while older files are being burned down. The
allowlist format is one rule per line:

    relative/path.cpp|line snippet

The snippet is matched as a substring against the stripped source line.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
import re


CPP_SUFFIXES = {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"}
DEFAULT_ALLOWLIST = "tools/cpp_auto_allowlist.txt"
WORD_AUTO = re.compile(r"\bauto\b")
STRUCTURED_BINDING = re.compile(r"\bauto(?:\s*[\*&])?\s*\[")
DECLARATION_AUTO = re.compile(
    r"^\s*(?:for\s*\(\s*)?(?:const\s+|constexpr\s+|static\s+|volatile\s+|mutable\s+)*"
    r"auto(?:\s*[\*&])?\s*(?:\w+|\[)"
)

ITERATOR_HINTS = (
    ".begin(",
    ".end(",
    ".cbegin(",
    ".cend(",
    ".rbegin(",
    ".rend(",
    ".crbegin(",
    ".crend(",
    ".find(",
    ".lower_bound(",
    ".upper_bound(",
    ".equal_range(",
    "std::begin(",
    "std::end(",
    "std::cbegin(",
    "std::cend(",
    "std::find(",
    "std::lower_bound(",
    "std::upper_bound(",
    "std::equal_range(",
    "std::max_element(",
    "std::min_element(",
    "std::prev(",
    "std::next(",
)


@dataclass(frozen=True, slots=True)
class AllowRule:
    relative_path: str
    snippet: str


@dataclass(frozen=True, slots=True)
class Violation:
    relative_path: str
    line_number: int
    line: str


def tracked_cpp_files(repo_root: Path) -> list[Path]:
    proc = subprocess.run(
        ["git", "ls-files"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    files: list[Path] = []
    for raw in proc.stdout.splitlines():
        path = repo_root / raw
        if path.suffix not in CPP_SUFFIXES:
            continue
        if any(part.startswith("build") for part in path.parts):
            continue
        if "_deps" in path.parts:
            continue
        files.append(path)
    return files


def read_allowlist(path: Path) -> list[AllowRule]:
    if not path.exists():
        return []
    rules: list[AllowRule] = []
    for raw in path.read_text().splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        relative_path, sep, snippet = stripped.partition("|")
        if not sep:
            raise ValueError(f"invalid allowlist rule: {raw!r}")
        rules.append(AllowRule(relative_path=relative_path.strip(), snippet=snippet.strip()))
    return rules


def is_lambda_closure(line: str) -> bool:
    if "=" not in line:
        return False
    eq_index = line.find("=")
    open_bracket = line.find("[", eq_index)
    close_bracket = line.find("]", open_bracket + 1) if open_bracket != -1 else -1
    return open_bracket != -1 and close_bracket != -1 and close_bracket > open_bracket


def is_iterator_like(line: str) -> bool:
    return any(hint in line for hint in ITERATOR_HINTS)


def is_allowed_auto(line: str) -> bool:
    stripped = line.strip()
    if "auto-ok" in stripped:
        return True
    if not DECLARATION_AUTO.search(stripped):
        return True
    if STRUCTURED_BINDING.search(stripped):
        return True
    if is_lambda_closure(stripped):
        return True
    if is_iterator_like(stripped):
        return True
    return False


def matches_allowlist(relative_path: str, line: str, rules: list[AllowRule]) -> bool:
    stripped = line.strip()
    for rule in rules:
        if rule.relative_path == relative_path and rule.snippet in stripped:
            return True
    return False


def collect_violations(repo_root: Path, allowlist: list[AllowRule]) -> list[Violation]:
    violations: list[Violation] = []
    for path in tracked_cpp_files(repo_root):
        relative_path = path.relative_to(repo_root).as_posix()
        for line_number, line in enumerate(path.read_text().splitlines(), start=1):
            if not WORD_AUTO.search(line):
                continue
            if is_allowed_auto(line):
                continue
            if matches_allowlist(relative_path, line, allowlist):
                continue
            violations.append(
                Violation(relative_path=relative_path, line_number=line_number, line=line.rstrip())
            )
    return violations


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    parser.add_argument("--write-allowlist", action="store_true")
    return parser.parse_args()


def write_allowlist(path: Path, violations: list[Violation]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Repo-owned exceptions for the explicit local-type policy.",
        "# Format: relative/path.cpp|line snippet",
    ]
    for violation in violations:
        lines.append(f"{violation.relative_path}|{violation.line.strip()}")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    allowlist_path = repo_root / args.allowlist
    allowlist = read_allowlist(allowlist_path)
    violations = collect_violations(repo_root, allowlist)
    if args.write_allowlist:
        write_allowlist(allowlist_path, violations)
        return 0
    if not violations:
        print("cpp_auto_audit: no disallowed local auto usage found")
        return 0
    print("cpp_auto_audit: disallowed local auto usage found", file=sys.stderr)
    for violation in violations:
        print(
            f"{violation.relative_path}:{violation.line_number}: {violation.line.strip()}",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
