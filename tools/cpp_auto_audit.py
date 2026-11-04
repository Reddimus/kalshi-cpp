#!/usr/bin/env python3
"""Audit repo-owned C++ sources for disallowed local ``auto`` usage.

Policy:
- allow ``auto`` for structured bindings
- allow ``auto`` for lambda closure objects
- allow ``auto`` for iterator-like values
- allow explicit one-off comments marked with ``auto-ok``

Everything else is expected to spell out the type directly. The optional
allowlist takes one rule per line:

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
    r"^\s*(?:}\s*)?(?:(?:else\s+)?if(?:\s+constexpr)?\s*\(\s*|(?:for|while|switch)\s*\(\s*)?(?:const\s+|constexpr\s+|static\s+|volatile\s+|mutable\s+)*"
    r"auto(?:\s*[\*&])?\s*(?:\w+|\[)"
)

ITERATOR_NAME = re.compile(r"\bauto\s*&?\s*(?:\w+_)?(?:it|iter|iterator)\b")

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
    # Include untracked, non-ignored files so a new test file is audited
    # locally before CI sees it.
    proc = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    files: list[Path] = []
    for raw in sorted(set(proc.stdout.split("\0")) - {""}):
        relative = Path(raw)
        if relative.suffix not in CPP_SUFFIXES:
            continue
        if any(part == "build" or part.startswith("build-") or part == "_deps" for part in relative.parts[:-1]):
            continue
        if not (repo_root / relative).exists():  # deleted but not yet staged
            continue
        files.append(repo_root / relative)
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


# Skip literals and comments before inspecting C++ delimiters. Keep numbers
# together so digit separators cannot be mistaken for character literals.
CPP_TOKEN = re.compile(
    r'(?P<skip>(?:u8|u|U|L)?R"(?P<raw>[^\s()\\]{0,16})\(.*?\)(?P=raw)"'
    r'|"(?:\\.|[^"\\])*"|\b\d[\w\'.]*|\'(?:\\.|[^\'\\])*\''
    r'|/\*.*?\*/|//[^\n]*)|[A-Za-z_]\w*|<<|[^\s]',
    re.DOTALL,
)


def lexical_context(source: str) -> tuple[str, set[int]]:
    """Mask literals/comments without moving code, and locate comment opt-outs."""
    pieces: list[str] = []
    opt_out_lines: set[int] = set()
    position = 0
    line_number = 1
    for token in CPP_TOKEN.finditer(source):
        gap = source[position:token.start()]
        pieces.append(gap)
        line_number += gap.count("\n")
        text = token[0]
        if token.lastgroup == "skip":
            if text.startswith(("//", "/*")):
                for number, comment_line in enumerate(text.split("\n"), start=line_number):
                    if "auto-ok" in comment_line:
                        opt_out_lines.add(number)
            pieces.append(re.sub(r"[^\n\r]", " ", text))
        else:
            pieces.append(text)
        line_number += text.count("\n")
        position = token.end()
    pieces.append(source[position:])
    return "".join(pieces), opt_out_lines


def direct_iterator_return(body: list[str], declaration: str) -> bool:
    # Recognize a single returned iterator call, not incidental calls elsewhere
    # in a body or an iterator comparison whose result is a bool.
    if not body or body[0] != "return" or body[-1] != ";":
        return False
    expression = body[1:-1]

    def closing_paren(tokens: list[str], start: int) -> int | None:
        depth = 0
        for index in range(start, len(tokens)):
            if tokens[index] == "(":
                depth += 1
            elif tokens[index] == ")":
                depth -= 1
                if depth == 0:
                    return index
        return None

    while (
        expression
        and expression[0] == "("
        and closing_paren(expression, 0) == len(expression) - 1
    ):
        expression = expression[1:-1]
    if "(" not in expression:
        return False
    opening = expression.index("(")
    callee = "".join(expression[:opening])
    if not re.fullmatch(r"(?:[A-Za-z_]\w*(?:::|\.|->))*[A-Za-z_]\w*", callee):
        return False
    return (
        closing_paren(expression, opening) == len(expression) - 1
        and is_iterator_like(f"{declaration} = {callee}(")
    )


def lambda_initializer_allowed(
    line: str, source: str | None = None, offset: int = 0
) -> bool | None:
    """Return None for another initializer, otherwise apply lambda/iterator rules."""
    equals = line.find("=")
    if equals < 0:
        return None
    tokens = (
        token[0]
        for token in CPP_TOKEN.finditer(source if source is not None else line, offset + equals + 1)
        if token.lastgroup != "skip"
    )
    if next(tokens, None) != "[":
        return None

    pairs = {"[": "]", "(": ")", "{": "}"}

    def consume_group(opener: str, contents: list[str] | None = None) -> bool:
        stack = [">" if opener == "<" else pairs[opener]]
        for token in tokens:
            if token in pairs:
                stack.append(pairs[token])
            elif token == "<" and stack[-1] == ">":
                stack.append(">")
            elif token == ">" and stack[-1] == ">":
                stack.pop()
                if not stack:
                    return True
            elif token in pairs.values():
                if token != stack.pop():
                    return False
                if not stack:
                    return True
            if contents is not None:
                contents.append(token)
        return False

    if not consume_group("["):
        return False
    # Parameter lists, attributes and noexcept/decltype expressions may contain
    # braces. The closure body starts at the next ungrouped opening brace.
    first_header_token = True
    requires_expression = False
    previous = None
    for token in tokens:
        if token == "requires" and previous in {"requires", "&", "|", "!", "?", ":"}:
            requires_expression = True
        if token == "{":
            if requires_expression:
                if not consume_group("{"):
                    return False
                requires_expression = False
                previous = token
                continue
            body: list[str] = []
            if not consume_group("{", body):
                return False
            following = next(tokens, None)
            if following == "(":
                if not consume_group("("):
                    return False
                return (
                    next(tokens, None) in {None, ";", ",", ")", "}"}
                    and direct_iterator_return(body, line.partition("=")[0])
                )
            return following in {None, ";", ",", ")", "}"}
        if token in {"(", "["} or (first_header_token and token == "<"):
            if not consume_group(token):
                return False
        elif token in {";", "}", "]", ")"}:
            return False
        first_header_token = False
        previous = token
    return False


def is_iterator_like(line: str) -> bool:
    # std::string::find returns an index, so .find( counts only when the
    # declared name reads as an iterator.
    if ".find(" in line and not ITERATOR_NAME.search(line):
        return False
    return any(hint in line for hint in ITERATOR_HINTS)


def is_allowed_auto(
    line: str, source: str | None = None, offset: int = 0, *, auto_ok: bool = False
) -> bool:
    stripped = line.strip()
    if auto_ok:
        return True
    if not DECLARATION_AUTO.search(stripped):
        return True
    if STRUCTURED_BINDING.search(stripped.partition("=")[0]):
        return True
    lambda_allowed = lambda_initializer_allowed(line, source, offset)
    if lambda_allowed is not None:
        return lambda_allowed
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
        source = path.read_text()
        code, opt_out_lines = lexical_context(source)
        offset = 0
        for line_number, line in enumerate(source.split("\n"), start=1):
            line_offset = offset
            offset += len(line) + 1
            code_line = code[line_offset:line_offset + len(line)]
            if not WORD_AUTO.search(code_line):
                continue
            if is_allowed_auto(code_line, source, line_offset, auto_ok=line_number in opt_out_lines):
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
    allowlist = [] if args.write_allowlist else read_allowlist(allowlist_path)
    violations = collect_violations(repo_root, [] if args.write_allowlist else allowlist)
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
