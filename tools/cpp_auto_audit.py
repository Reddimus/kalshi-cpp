#!/usr/bin/env python3
"""Audit repo-owned C++ sources for disallowed local ``auto`` usage.

Policy (``decltype(auto)`` follows the same rules):
- allow ``auto`` for structured bindings
- allow ``auto`` for lambda closure objects
- allow ``auto`` when the initializer is one iterator call, such as
  ``values.begin()``, ``table.find(key)``, or ``std::next(it)``
- allow explicit one-off comments marked with ``auto-ok``

Everything else is expected to spell out the type directly. The optional
allowlist takes one rule per line:

    relative/path.cpp|line snippet

The snippet, which must not be empty, is matched as a substring against the
stripped source line.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
import re


# The Makefile's CPP_SOURCES formats the same suffixes; keep the two lists equal.
CPP_SUFFIXES = {".cpp", ".hpp", ".h", ".ipp", ".inl", ".cc", ".cxx", ".hh", ".hxx"}
DEFAULT_ALLOWLIST = "tools/cpp_auto_allowlist.txt"
WORD_AUTO = re.compile(r"\bauto\b")
DECL_SPECIFIERS = {
    "const", "constexpr", "static", "volatile", "mutable",
    "constinit", "thread_local", "extern", "inline",
}
DECL_SPECIFIER = "(?:" + "|".join(sorted(DECL_SPECIFIERS)) + ")"
AUTO_KEYWORD = r"(?:\bdecltype\s*\(\s*auto\s*\)|\bauto\b)"
AUTO_DECLARATOR = rf"{AUTO_KEYWORD}(?:\s*(?:[\*&]|{DECL_SPECIFIER}\b))*\s*"
STRUCTURED_BINDING = re.compile(AUTO_DECLARATOR + r"\[")
DECLARATION_AUTO = re.compile(
    r"^\s*(?:}\s*)?(?:(?:else\s+)?if(?:\s+constexpr)?\s*\(\s*|(?:for|while|switch)\s*\(\s*)?"
    + rf"(?:{DECL_SPECIFIER}\s+)*"
    + AUTO_DECLARATOR
    + rf"(?:(?!{DECL_SPECIFIER}\b)\w+\b(?!\s*[,)])|\[)"
)

ITERATOR_NAME = re.compile(AUTO_DECLARATOR + r"(?:\w+_)?(?:it|iter|iterator)\b")

# Calls whose result is an iterator: member functions reached with `.`, and
# free functions by their full name.
ITERATOR_METHODS = {
    "begin", "end", "cbegin", "cend", "rbegin", "rend", "crbegin", "crend",
    "find", "lower_bound", "upper_bound", "equal_range",
}
ITERATOR_FUNCTIONS = {
    "std::begin", "std::end", "std::cbegin", "std::cend", "std::find",
    "std::lower_bound", "std::upper_bound", "std::equal_range",
    "std::max_element", "std::min_element", "std::prev", "std::next",
    "std::find_if", "std::find_if_not", "std::find_first_of", "std::find_end",
    "std::adjacent_find", "std::search", "std::search_n", "std::partition_point",
    "std::is_sorted_until", "std::remove", "std::remove_if", "std::unique",
}
# A callee made of names joined by `::`, `.`, or `->`, with calls and
# subscripts reduced to `()` and `[]`.
POSTFIX_CHAIN = re.compile(r"(?:::)?[A-Za-z_]\w*(?:(?:::|\.|->)[A-Za-z_]\w*|\(\)|\[\])*")
GROUP_PAIRS = {"(": ")", "[": "]", "{": "}"}


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
        if not snippet.strip():
            # An empty snippet is a substring of every line, so it would exempt the whole file.
            raise ValueError(f"allowlist rule has an empty snippet: {raw!r}")
        rules.append(AllowRule(relative_path=relative_path.strip(), snippet=snippet.strip()))
    return rules


# Skip literals and comments before inspecting C++ delimiters. Keep numbers
# together so digit separators cannot be mistaken for character literals.
CPP_TOKEN = re.compile(
    r'(?P<skip>(?:u8|u|U|L)?R"(?P<raw>[^\s()\\]{0,16})\(.*?\)(?P=raw)"'
    r'|"(?:\\.|[^"\\\r\n])*"|\b\d[\w\'.]*|\'(?:\\.|[^\'\\\r\n])*\''
    r'|/\*.*?\*/|//[^\n]*)|[A-Za-z_]\w*|<<|[^\s]',
    re.DOTALL,
)


def declaration_code(line: str) -> str:
    """Hide declaration groups while preserving initializer character offsets."""
    tokens = list(CPP_TOKEN.finditer(line))
    code = list(line)

    def hide_group(start: int, opener: str, closer: str) -> int:
        depth = 0
        for end in range(start, len(tokens)):
            text = tokens[end][0]
            if text == opener:
                depth += 1
            elif text == closer:
                depth -= 1
                if depth == 0:
                    code[tokens[start].start():tokens[end].end()] = " " * (
                        tokens[end].end() - tokens[start].start()
                    )
                    return end + 1
        return len(tokens)

    index = 0
    while index < len(tokens):
        text = tokens[index][0]
        if text == "[" and index + 1 < len(tokens) and tokens[index + 1][0] == "[":
            index = hide_group(index, "[", "]")
        elif text == "alignas" and index + 1 < len(tokens) and tokens[index + 1][0] == "(":
            code[tokens[index].start():tokens[index].end()] = " " * len(text)
            index = hide_group(index + 1, "(", ")")
        elif text == "auto":
            if (
                index >= 2
                and [token[0] for token in tokens[index - 2:index]] == ["decltype", "("]
                and index + 1 < len(tokens)
                and tokens[index + 1][0] == ")"
            ):
                index += 1  # the `)` that closes decltype(auto)
            index += 1
            break
        else:
            index += 1
    parentheses = 0
    while index < len(tokens):
        text = tokens[index][0]
        if text in {"*", "&"} or text in DECL_SPECIFIERS:
            index += 1
        elif text == "(":
            code[tokens[index].start()] = " "
            parentheses += 1
            index += 1
        else:
            break
    if index < len(tokens) and re.fullmatch(r"[A-Za-z_]\w*", tokens[index][0]):
        index += 1
        while index < len(tokens) and tokens[index][0] == ")" and parentheses:
            code[tokens[index].start()] = " "
            parentheses -= 1
            index += 1
        while index + 1 < len(tokens) and tokens[index][0] == tokens[index + 1][0] == "[":
            index = hide_group(index, "[", "]")
    return "".join(code)


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
        and iterator_callee(callee, declaration)
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
    first = next(tokens, None)
    parentheses = 0
    while first == "(":
        parentheses += 1
        first = next(tokens, None)
    if first != "[":
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
            invoked = False
            while True:
                if following == ")" and parentheses:
                    parentheses -= 1
                    following = next(tokens, None)
                elif following == "(" and not invoked:
                    if not consume_group("("):
                        return False
                    invoked = True
                    following = next(tokens, None)
                else:
                    break
            if parentheses or following not in {None, ";", ",", ")", "}"}:
                return False
            return not invoked or direct_iterator_return(body, line.partition("=")[0])
        if token in {"(", "["} or (first_header_token and token == "<"):
            if not consume_group(token):
                return False
        elif token in {";", "}", "]", ")"}:
            return False
        first_header_token = False
        previous = token
    return False


def iterator_callee(callee: str, declaration: str) -> bool:
    """Whether calling ``callee``, such as ``values.begin`` or ``std::next``, yields an iterator."""
    method = re.fullmatch(r".+\.([A-Za-z_]\w*)", callee)
    if method is None:
        return callee in ITERATOR_FUNCTIONS
    # std::string::find returns an index, so .find counts only when the
    # declared name reads as an iterator.
    if method[1] == "find" and not ITERATOR_NAME.search(declaration):
        return False
    return method[1] in ITERATOR_METHODS


def group_end(tokens: list[str], start: int) -> int | None:
    """Index of the token closing the group that opens at ``start``."""
    closers: list[str] = []
    for index in range(start, len(tokens)):
        if tokens[index] in GROUP_PAIRS:
            closers.append(GROUP_PAIRS[tokens[index]])
        elif tokens[index] in GROUP_PAIRS.values():
            if not closers or tokens[index] != closers.pop():
                return None
            if not closers:
                return index
    return None


def initializer_tokens(line: str, source: str | None = None, offset: int = 0) -> list[str] | None:
    """Code tokens of the declaration's ``= x``, ``{x}``, or ``(x)`` initializer.

    Reading from ``source`` lets an initializer continue on later lines.
    Comments and literals are left out.
    """
    declarator = DECLARATION_AUTO.search(line)
    if declarator is None:
        return None
    position = len(line) - len(line[declarator.end():].lstrip())
    if position >= len(line) or line[position] not in "={(":
        return None
    direct = line[position] != "="
    stream = (
        token[0]
        for token in CPP_TOKEN.finditer(source if source is not None else line, offset + position + (not direct))
        if token.lastgroup != "skip"
    )
    closers = [GROUP_PAIRS[next(stream)]] if direct else []
    depth = len(closers)
    tokens: list[str] = []
    for token in stream:
        if token in GROUP_PAIRS:
            closers.append(GROUP_PAIRS[token])
        elif token in GROUP_PAIRS.values():
            if not closers:
                return tokens  # the `)` of `if (`, `for (`, or `while (`
            if token != closers.pop():
                return None
            if direct and not closers:
                return tokens  # the initializer's own group
        elif token in {";", ","} and len(closers) == depth:
            # The next declarator or the end of the statement. Inside a direct
            # initializer, a comma separates constructor arguments.
            return None if direct else tokens
        tokens.append(token)
    return None if direct else tokens


def iterator_expression(tokens: list[str], declaration: str) -> bool:
    """Whether ``tokens`` are one iterator call, such as ``values.begin()`` or ``std::next(it)``.

    Calls whose arguments use iterators, such as ``std::distance(values.begin(),
    values.end())``, and expressions built from an iterator, such as
    ``*values.begin()`` or ``values.begin() + 1``, do not count.
    """
    while tokens and tokens[0] == "(" and group_end(tokens, 0) == len(tokens) - 1:
        tokens = tokens[1:-1]
    shape: list[str] = []
    index = 0
    while index < len(tokens):
        if tokens[index] in {"(", "["}:
            end = group_end(tokens, index)
            if end is None:
                return False
            shape.append(tokens[index] + GROUP_PAIRS[tokens[index]])
            index = end + 1
        else:
            shape.append(tokens[index])
            index += 1
    if len(shape) < 2 or shape[-1] != "()":
        return False
    callee = "".join(shape[:-1])
    return bool(POSTFIX_CHAIN.fullmatch(callee)) and iterator_callee(callee.removeprefix("::"), declaration)


def is_allowed_auto(
    line: str, source: str | None = None, offset: int = 0, *, auto_ok: bool = False
) -> bool:
    line = declaration_code(line)
    stripped = line.strip()
    if auto_ok:
        return True
    if not DECLARATION_AUTO.search(stripped):
        return True
    if STRUCTURED_BINDING.search(stripped.partition("=")[0]):
        return True
    lambda_allowed = lambda_initializer_allowed(line, source, offset)
    if lambda_allowed is not None:
        return lambda_allowed and later_declarators_allowed(line, source, offset)
    tokens = initializer_tokens(line, source, offset)
    return (
        tokens is not None
        and iterator_expression(tokens, line.partition("=")[0])
        and later_declarators_allowed(line, source, offset)
    )


def later_declarators(line: str, source: str | None = None, offset: int = 0) -> list[str]:
    """The declarators after the first in an ``auto`` declaration, each as its own declaration.

    ``auto a = x, b = y;`` gives ``auto b = y ;``. Reading from ``source`` lets the
    declaration continue on later lines. A lambda's template parameter list, as in
    ``[]<typename A, typename B>``, is skipped, so its commas do not split anything.
    """
    declarator = DECLARATION_AUTO.search(line)
    if declarator is None:
        return []
    text = source if source is not None else line
    tokens = [token[0] for token in CPP_TOKEN.finditer(text, offset + declarator.end()) if token.lastgroup != "skip"]
    closers: list[str] = []
    pieces: list[list[str]] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        index += 1
        if token == "<" and index >= 2 and tokens[index - 2] == "]":
            depth = 1
            while index < len(tokens) and depth:
                depth += {"<": 1, ">": -1}.get(tokens[index], 0)
                index += 1
            continue
        if token in GROUP_PAIRS:
            closers.append(GROUP_PAIRS[token])
        elif token in GROUP_PAIRS.values():
            if not closers:
                break  # the `)` that closes `if (`, `for (`, or `while (`
            if token != closers.pop():
                return []
        elif not closers and token == ";":
            break
        elif not closers and token == ",":
            pieces.append([])
            continue
        if pieces:
            pieces[-1].append(token)
    return ["auto " + " ".join(piece) + " ;" for piece in pieces]


def later_declarators_allowed(line: str, source: str | None = None, offset: int = 0) -> bool:
    """Whether each declarator after the first would be allowed as its own ``auto`` declaration."""
    return all(is_allowed_auto(declaration) for declaration in later_declarators(line, source, offset))


STATEMENT_BOUNDARIES = {";", "{", "}"}


def auto_statements(code: str, start: int, end: int) -> list[tuple[int, int]]:
    """Spans of ``code`` to audit for each ``auto`` on the line ``code[start:end]``.

    ``code`` has literals and comments masked. A span starts after the last
    ``;``, ``{``, or ``}`` before its ``auto``, so ``int a = 0; auto b = f();``
    checks ``auto b = f();``. When ``auto`` and its specifiers end the line, the
    span runs through the line that holds the declarator.
    """
    tokens = [token for token in CPP_TOKEN.finditer(code, start, end) if token.lastgroup != "skip"]
    spans: dict[int, int] = {}
    statement = start
    for index, token in enumerate(tokens):
        if token[0] in STATEMENT_BOUNDARIES:
            statement = token.end()
            continue
        if token[0] != "auto" or statement in spans:
            continue
        after = index + 1
        if (
            [t[0] for t in tokens[max(index - 2, 0):index]] == ["decltype", "("]
            and after < len(tokens)
            and tokens[after][0] == ")"
        ):
            after += 1
        while after < len(tokens) and (tokens[after][0] in {"*", "&"} or tokens[after][0] in DECL_SPECIFIERS):
            after += 1
        span_end = end
        if after == len(tokens):
            for following in CPP_TOKEN.finditer(code, end):
                if following.lastgroup == "skip" or following[0] in {"*", "&"} or following[0] in DECL_SPECIFIERS:
                    continue
                line_end = code.find("\n", following.start())
                span_end = len(code) if line_end < 0 else line_end
                break
        spans[statement] = span_end
    return list(spans.items())


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
            # Newlines become spaces, so a span keeps the offsets of `source`.
            if line_number in opt_out_lines or all(
                is_allowed_auto(code[span_start:span_end].replace("\n", " "), source, span_start)
                for span_start, span_end in auto_statements(code, line_offset, line_offset + len(line))
            ):
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
