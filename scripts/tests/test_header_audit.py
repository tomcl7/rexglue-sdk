import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from header_audit import Finding, audit_file, load_allowlist, run  # noqa: E402

BLOCK = (
    "/**\n"
    " * @file        {path}\n"
    " * @brief       Test header\n"
    " *\n"
    " * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>\n"
    " *              All rights reserved.\n"
    " *\n"
    " * @license     BSD 3-Clause License\n"
    " *              See LICENSE file in the project root for full license text.\n"
    " */\n"
    "\n"
    "#pragma once\n\n"
)


def kinds(findings: list[Finding]) -> list[str]:
    return sorted({f.kind for f in findings})


def audit(text: str, rel: str = "sample.h") -> list[Finding]:
    root = Path("/virtual/include/rex")
    return audit_file(root / rel, text, root)


def test_clean_header_has_no_findings():
    text = BLOCK.format(path="sample.h") + (
        "namespace rex {\n\n"
        "/**\n * Adds 1.\n */\n"
        "int AddOne(int value);\n\n"
        "/**\n * A macro.\n */\n"
        "#define REX_SAMPLE 1\n\n"
        "}  // namespace rex\n"
    )
    assert audit(text) == []


def test_missing_copyright_block():
    text = "#pragma once\nnamespace rex {}\n"
    assert "copyright" in kinds(audit(text))


def test_public_declaration_without_doxygen():
    text = BLOCK.format(path="sample.h") + "namespace rex {\nint AddOne(int value);\n}\n"
    assert "doxygen" in kinds(audit(text))


def test_private_members_are_not_checked():
    text = BLOCK.format(path="sample.h") + (
        "namespace rex {\n/**\n * A class.\n */\nclass Thing {\n private:\n  int Hidden();\n};\n}\n"
    )
    assert "doxygen" not in kinds(audit(text))


def test_namespace_outside_map():
    text = BLOCK.format(path="sample.h") + "namespace rex::bogus {\n}\n"
    assert "namespace" in kinds(audit(text))


def test_detail_namespace_is_allowed_and_unchecked():
    text = BLOCK.format(path="sample.h") + "namespace rex::detail {\nint Helper();\n}\n"
    assert audit(text) == []


def test_macro_prefix():
    text = BLOCK.format(path="sample.h") + "/**\n * Bad.\n */\n#define SAMPLE 1\n"
    assert "macro" in kinds(audit(text))


def test_xenia_and_guest_tokens():
    text = BLOCK.format(path="sample.h") + "// guest memory\nint x = XE_THING;\n"
    found = kinds(audit(text))
    assert "guest" in found and "xenia" in found


def test_frozen_hook_header_is_exempt_from_xenia_and_prefix():
    text = "// legacy\n#define XE_EXPORT(a) a\n"
    assert audit(text, "hook.h") == []


def test_allowlist_skips_files(tmp_path: Path):
    root = tmp_path / "include" / "rex"
    root.mkdir(parents=True)
    (root / "bad.h").write_text("int x;\n", encoding="utf-8")
    allow = tmp_path / "allow.txt"
    allow.write_text("bad.h\n", encoding="utf-8")
    assert run(root, load_allowlist(allow)) == []
    assert run(root, set()) != []


def test_forward_declarations_and_special_members_need_no_block():
    text = BLOCK.format(path="sample.h") + (
        "namespace rex {\n"
        "class Later;\n"
        "/**\n * A thing.\n */\n"
        "class Thing {\n"
        " public:\n"
        "  Thing() = default;\n"
        "  ~Thing();\n"
        "  Thing(const Thing&) = delete;\n"
        "  Thing(Thing&& other) noexcept;\n"
        "  Thing& operator=(Thing&& other) noexcept;\n"
        "};\n"
        "}\n"
    )
    assert audit(text) == []


def test_conditional_define_and_macro_body_are_documented_once():
    text = BLOCK.format(path="sample.h") + (
        "/**\n * Export visibility.\n */\n"
        "#if REX_PLATFORM_WIN32\n"
        "#define REX_API __declspec(dllexport)\n"
        "#else\n"
        "#define REX_API __attribute__((visibility(\"default\")))\n"
        "#endif\n"
        "/**\n * Pasting helpers.\n */\n"
        "#define REX_A(x) x\n"
        "#define REX_B(x) REX_A(x)\n"
        "/**\n * A body.\n */\n"
        "#define REX_BODY(T) \\\n"
        "  extern \"C\" void f(T* value) { \\\n"
        "    return; \\\n"
        "  }\n"
    )
    assert audit(text) == []


def test_legacy_namespace_is_accepted():
    text = BLOCK.format(path="sample.h") + "namespace rex::system {\nclass KernelState;\n}\n"
    assert audit(text) == []
