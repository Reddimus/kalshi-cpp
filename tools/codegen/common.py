"""Naming, documentation, and emission helpers shared by the REST and WebSocket generators."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ACRONYMS = {"RFQs": "Rfqs", "RFQ": "Rfq", "FCM": "Fcm", "ID": "Id", "API": "Api", "MVE": "Mve"}

CPP_RESERVED = {
    "and", "auto", "bool", "break", "case", "catch", "char", "class", "const", "continue",
    "default", "delete", "do", "double", "else", "enum", "explicit", "export", "extern",
    "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
    "namespace", "new", "not", "operator", "or", "private", "protected", "public", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw",
    "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "while", "xor",
}

# A server value spelled "unknown" shares the Unknown enumerator every enum has.
UNKNOWN_VALUE = "unknown"


def snake(name: str) -> str:
    for acronym, word in ACRONYMS.items():
        name = name.replace(acronym, word)
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


def pascal(name: str) -> str:
    parts = re.split(r"[^0-9A-Za-z]+", name)
    return "".join(part[:1].upper() + part[1:] for part in parts if part)


def enumerator(value: str) -> str:
    if value == "":
        return "Unset"  # e.g. a market result before settlement
    text = pascal(value.replace("::", "_"))
    return text if text and not text[0].isdigit() else "V" + text


def ref_name(ref: str) -> str:
    return ref.rsplit("/", 1)[-1]


def first_sentence(text: str | None) -> str:
    if not text:
        return ""
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)  # markdown links -> text
    text = re.sub(r"<[^>]+>", "", text)
    text = " ".join(text.split())
    match = re.match(r"(.+?[.!?])(\s|$)", text)
    sentence = match.group(1) if match else text
    return sentence if len(sentence) <= 300 else sentence[:297].rstrip() + "..."


def doc_lines(text: str, indent: str, width: int = 96) -> list[str]:
    sentence = first_sentence(text)
    if not sentence:
        return []
    return [f"{indent}/// {line}" for line in textwrap.wrap(sentence, width - len(indent) - 4)]


@dataclass
class EnumType:
    name: str
    values: list[str]
    doc: str = ""

    @property
    def known(self) -> list[str]:
        """Values with their own enumerator; "unknown" maps to Unknown."""
        return [v for v in self.values if v != UNKNOWN_VALUE]


@dataclass
class Member:
    json_name: str
    cpp_type: str
    doc: str = ""
    fixed_point: bool = False  # FixedPointDollars / FixedPointCount string
    required: bool = False
    base_kind: str = ""  # "string", "enum", "struct", "vector", ...
    element: str = ""  # struct or enum name for validation


@dataclass
class StructType:
    name: str
    members: list[Member] = field(default_factory=list)
    doc: str = ""


def check_enums(enums: dict[str, EnumType]) -> None:
    for enum in enums.values():
        names = [enumerator(v) for v in enum.known]
        if len(set(names)) != len(names) or "Unknown" in names:
            raise SystemExit(f"enum {enum.name} has colliding enumerators: {names}")


def ordered_structs(structs: dict[str, StructType]) -> list[StructType]:
    """Structs in dependency order, so each is defined before it is used."""
    order: list[StructType] = []
    state: dict[str, int] = {}

    def visit(name: str) -> None:
        if state.get(name) == 2 or name not in structs:
            return
        if state.get(name) == 1:
            raise SystemExit(f"cycle through {name}")
        state[name] = 1
        for m in structs[name].members:
            if m.element in structs:
                visit(m.element)
        state[name] = 2
        order.append(structs[name])

    for name in sorted(structs):
        visit(name)
    return order


def emit_enum(enum: EnumType) -> str:
    out = [line + "\n" for line in doc_lines(enum.doc, "")]
    unknown_doc = "`unknown`, or a value this SDK version does not know." if UNKNOWN_VALUE in enum.values \
        else "A value this SDK version does not know."
    out.append(f"enum class {enum.name} : std::uint8_t {{\n\tUnknown, ///< {unknown_doc}\n")
    for value in enum.known:
        doc = f"`{value}`" if value else "empty string"
        out.append(f"\t{enumerator(value)}, ///< {doc}\n")
    out.append("};\n\n")
    out.append(f"[[nodiscard]] constexpr std::string_view to_string({enum.name} value) noexcept {{\n\tswitch (value) {{\n")
    for value in enum.known:
        out.append(f"\t\tcase {enum.name}::{enumerator(value)}:\n\t\t\treturn \"{value}\";\n")
    fallback = f'\t\t\treturn "{UNKNOWN_VALUE}";\n' if UNKNOWN_VALUE in enum.values else "\t\t\tbreak;\n"
    out.append(f"\t\tcase {enum.name}::Unknown:\n{fallback}\t}}\n\treturn \"\";\n}}\n\n")
    return "".join(out)


def emit_enum_parser(enum: EnumType, qualified: str, prefix: str = "") -> str:
    out = [f"inline {qualified} parse_{prefix}{snake(enum.name)}(std::string_view text) noexcept {{\n"]
    for value in enum.known:
        test = "text.empty()" if value == "" else f'text == "{value}"'
        out.append(f"\tif ({test}) {{\n\t\treturn {qualified}::{enumerator(value)};\n\t}}\n")
    out.append(f"\treturn {qualified}::Unknown;\n}}\n\n")
    return "".join(out)


def emit_enum_adapter(enum: EnumType, qualified: str, parser: str) -> str:
    """Glaze adapter that reads unknown strings as Unknown."""
    return (
        f"template <>\nstruct from<JSON, {qualified}> {{\n\ttemplate <auto Opts>\n"
        f"\tstatic void op({qualified}& value, auto&& ctx, auto&& it, auto&& end) {{\n"
        "\t\tstd::string text;\n\t\tparse<JSON>::op<Opts>(text, ctx, it, end);\n"
        f"\t\tvalue = {parser}(text);\n\t}}\n}};\n\n"
        f"template <>\nstruct to<JSON, {qualified}> {{\n\ttemplate <auto Opts>\n"
        f"\tstatic void op(const {qualified}& value, auto&& ctx, auto&&... args) {{\n"
        f"\t\tconst std::string_view text = {qualified.rsplit('::', 1)[0]}::to_string(value);\n"
        "\t\tserialize<JSON>::op<Opts>(text, ctx, args...);\n\t}\n};\n\n")


def emit_struct(struct: StructType) -> str:
    out = [line + "\n" for line in doc_lines(struct.doc, "")]
    out.append(f"struct {struct.name} {{\n")
    for m in struct.members:
        out.extend(line + "\n" for line in doc_lines(m.doc, "\t"))
        init = "{}" if m.required and m.base_kind in ("scalar", "enum") else ""
        out.append(f"\t{m.cpp_type} {m.json_name}{init};\n")
    out.append("};\n\n")
    return "".join(out)


def clang_format(path: Path, text: str) -> str:
    """Formats generated C++ with the repository style so lint and --check agree."""
    # `make` passes the clang-format it checks with, so both steps use one binary.
    binary = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format-18") or shutil.which("clang-format")
    if binary is None:
        sys.exit("clang-format 18 is required to generate C++ sources")
    version = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True).stdout
    if not re.search(r"version 18\.", version):
        sys.exit(f"clang-format 18 is required; found: {version.strip()}")
    result = subprocess.run([binary, f"--assume-filename={path}"], input=text, capture_output=True,
                            text=True, check=True, cwd=ROOT)
    return result.stdout
