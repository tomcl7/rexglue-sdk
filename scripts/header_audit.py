"""Audit public headers under include/rex against the charter rules.

Findings, each reported as path:line: text:
  copyright   the file does not start with the exact copyright block
  doxygen     a public declaration has no /** */ block directly above it
  namespace   a namespace outside the charter map
  macro       a #define whose name lacks an allowed prefix
  xenia       an XE_ macro or xe:: symbol outside the frozen files
  guest       the word guest in a comment or identifier
Files listed in the allowlist are skipped entirely.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

COPYRIGHT = (
    "/**\n"
    " * @file        {path}\n"
    " * @brief       "
)
COPYRIGHT_TAIL = (
    " * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>\n"
    " *              All rights reserved.\n"
    " *\n"
    " * @license     BSD 3-Clause License\n"
    " *              See LICENSE file in the project root for full license text.\n"
)

PUBLIC_NAMESPACES = {
    "rex", "rex::platform", "rex::log", "rex::cvar", "rex::xenos", "rex::ppc",
    "rex::filesystem", "rex::runtime", "rex::audio", "rex::input", "rex::ui",
    "rex::graphics", "rex::codegen",
}
# Namespaces that exist until a later sub-project folds them into the charter map
# (charter 11.1). Each sub-project removes the entries it folds.
LEGACY_NAMESPACES = {
    "rex::system", "rex::kernel", "rex::memory", "rex::thread", "rex::arch", "rex::debug",
    "rex::net", "rex::perf", "rex::stream", "rex::string", "rex::bit", "rex::literals",
    "rex::chrono", "rex::platform::lib_names",
}
MACRO_PREFIXES = ("REX_", "REXLOG_", "REXCVAR_", "REXGPU_")
FORWARD_DECL_RE = re.compile(r"^\s*(?:class|struct|union|enum(?:\s+class)?)\s+[\w:]+\s*;\s*$")
SPECIAL_MEMBER_RE = re.compile(
    r"=\s*(?:default|delete)\s*;\s*$"
    r"|^\s*(?:virtual\s+)?~\w+\s*\("
    r"|^\s*(\w+)\s*\((?:const\s+)?\1\s*&&?\s*\w*\)\s*(?:noexcept)?\s*;"
    r"|^\s*\w+&\s+operator=\s*\("
)
FROZEN_FILES = {"hook.h"}
FROZEN_SUFFIXES = ("export_table.inc",)

DECL_RE = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?"
    r"(?:(?:class|struct|enum(?:\s+class)?|union)\s+\w+|"
    r"using\s+\w+\s*=|"
    r"(?:inline\s+|static\s+|constexpr\s+|virtual\s+|explicit\s+|extern\s+\"C\"\s+)*"
    r"[\w:<>,\s\*&]+?\s+[\w:~]+\s*\([^;]*;?\s*$|"
    r"#define\s+\w+)"
)
ACCESS_RE = re.compile(r"^\s*(public|protected|private)\s*:")
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([\w:]+)\s*\{")
ANON_NAMESPACE_RE = re.compile(r"^\s*namespace\s*\{")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+(\w+)")
XENIA_RE = re.compile(r"\bXE_\w+|\bxe::")
GUEST_RE = re.compile(r"\bguest\b", re.IGNORECASE)
FIRST_TOKEN_RE = re.compile(r"^\s*(\w+)")
STATEMENT_KEYWORDS = {
    "if", "for", "while", "switch", "catch", "else", "return", "do",
    "throw", "case", "goto", "break", "continue", "delete", "new", "sizeof",
}


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    kind: str
    text: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.kind}: {self.text}"


def _relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def audit_copyright(rel: str, text: str) -> list[Finding]:
    head = COPYRIGHT.format(path=rel)
    if not text.startswith(head):
        return [Finding(rel, 1, "copyright", "file does not start with the standard @file block")]
    end = text.find(" */")
    block = text[: end + 3] if end >= 0 else text
    if COPYRIGHT_TAIL not in block:
        return [Finding(rel, 1, "copyright", "copyright or license line differs from the standard block")]
    return []


def audit_body(rel: str, text: str, frozen: bool) -> list[Finding]:
    findings: list[Finding] = []
    lines = text.splitlines()
    namespace_stack: list[str] = []
    brace_depth_at_namespace: list[int] = []
    depth = 0
    in_private = False
    in_block_comment = False
    prev_doc_end = -1  # line index where the last /** */ block ended

    for index, raw in enumerate(lines):
        line = raw.rstrip("\n")
        stripped = line.strip()
        line_no = index + 1

        if GUEST_RE.search(line) and not frozen:
            findings.append(Finding(rel, line_no, "guest", "the word guest"))
        if not frozen and XENIA_RE.search(line):
            findings.append(Finding(rel, line_no, "xenia", "XE_ macro or xe:: symbol"))

        if in_block_comment:
            if "*/" in stripped:
                in_block_comment = False
                prev_doc_end = index
            continue
        if stripped.startswith("/**"):
            if "*/" in stripped:
                prev_doc_end = index
            else:
                in_block_comment = True
            continue
        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_block_comment = True
            continue
        if stripped.startswith("//") or not stripped:
            continue

        if index > 0 and lines[index - 1].rstrip().endswith("\\"):
            # Inside a macro body: continuation lines are not declarations.
            continue

        match = DEFINE_RE.match(line)
        if match:
            name = match.group(1)
            if not name.startswith(MACRO_PREFIXES) and not frozen:
                findings.append(Finding(rel, line_no, "macro", f"macro {name} lacks a REX_ prefix"))
            # The block may precede an enclosing #if/#else, or a previous #define in the same group.
            probe = index - 1
            while probe >= 0 and (lines[probe].strip().startswith("#") or not lines[probe].strip()):
                if lines[probe].strip().startswith("#define") or lines[probe].strip().startswith("# define"):
                    break
                probe -= 1
            documented = prev_doc_end == probe or (
                probe >= 0 and DEFINE_RE.match(lines[probe]) is not None
            )
            if not documented and not frozen:
                findings.append(Finding(rel, line_no, "doxygen", f"macro {name} has no Doxygen block"))
            continue

        anon = ANON_NAMESPACE_RE.match(line)
        ns = NAMESPACE_RE.match(line)
        if anon or ns:
            name = "detail" if anon else ns.group(1)
            full = "::".join(namespace_stack + [name]) if namespace_stack else name
            leaf = full.split("::")[-1]
            if (
                leaf != "detail"
                and full not in PUBLIC_NAMESPACES
                and full not in LEGACY_NAMESPACES
                and not frozen
            ):
                findings.append(Finding(rel, line_no, "namespace", f"namespace {full} is not in the charter map"))
            namespace_stack.append(name)
            brace_depth_at_namespace.append(depth)
            depth += line.count("{") - line.count("}")
            continue

        if ACCESS_RE.match(line):
            in_private = stripped.startswith("private")
            continue

        in_detail = any(ns.split("::")[-1] == "detail" for ns in namespace_stack)
        first_token_match = FIRST_TOKEN_RE.match(line)
        first_token = first_token_match.group(1) if first_token_match else ""
        is_init_list = stripped.startswith(":") and not stripped.startswith("::")
        if (
            not in_private
            and not in_detail
            and not frozen
            and not is_init_list
            and first_token not in STATEMENT_KEYWORDS
            and DECL_RE.match(line)
            and not stripped.startswith("#")
            and not FORWARD_DECL_RE.match(line)
            and not SPECIAL_MEMBER_RE.search(line)
            and not stripped.startswith("REX_INTERFACE(")
        ):
            attr_or_template = index > 0 and lines[index - 1].strip().startswith(("[[", "template"))
            doc_index = index - 2 if attr_or_template else index - 1
            if prev_doc_end != doc_index:
                findings.append(Finding(rel, line_no, "doxygen", f"public declaration without a Doxygen block: {stripped[:60]}"))

        depth += line.count("{") - line.count("}")
        while brace_depth_at_namespace and depth <= brace_depth_at_namespace[-1]:
            namespace_stack.pop()
            brace_depth_at_namespace.pop()
            in_private = False
        if stripped.startswith("};"):
            in_private = False

    return findings


def audit_file(path: Path, text: str, root: Path) -> list[Finding]:
    rel = _relative(path, root)
    frozen = path.name in FROZEN_FILES or rel.endswith(FROZEN_SUFFIXES)
    findings = [] if frozen else audit_copyright(rel, text)
    findings += audit_body(rel, text, frozen)
    return findings


def load_allowlist(path: Path | None) -> set[str]:
    if path is None or not path.exists():
        return set()
    entries = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            entries.add(line.replace("\\", "/"))
    return entries


def run(root: Path, allowlist: set[str]) -> list[Finding]:
    findings: list[Finding] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in {".h", ".inc"}:
            continue
        rel = _relative(path, root)
        if rel in allowlist:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        findings += audit_file(path, text, root)
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--allowlist", type=Path, default=None)
    args = parser.parse_args(argv)
    findings = run(args.root, load_allowlist(args.allowlist))
    for finding in findings:
        print(finding)
    print(f"{len(findings)} finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
