"""Exercise the explicit-type audit through its command-line interface."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("cpp_auto_audit.py")


class AutoAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="builder-audit-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)

    def audit(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), "--repo-root", str(self.root), *args], capture_output=True, text=True, check=False)

    def test_builder_parent_does_not_hide_array_value(self):
        (self.root / "value.cpp").write_text("auto value = values[0];\n")
        result = self.audit()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("value.cpp:1", result.stderr)

    def test_builder_sources_are_not_build_artifacts(self):
        folder = self.root / "src/buildsys"
        folder.mkdir(parents=True)
        (folder / "builder.cpp").write_text("auto value = 42;\n")
        result = self.audit()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/buildsys/builder.cpp:1", result.stderr)

    def test_conditional_scalar_initializers_are_checked(self):
        (self.root / "condition.cpp").write_text("if (auto value = compute(); value) {}\nswitch (auto value = compute(); value) {}\n} else if (auto value = compute(); value) {}\nwhile (auto value = compute()) {}\nif constexpr (auto value = compute(); value) {}\n")
        result = self.audit()
        self.assertEqual(result.returncode, 1)
        self.assertIn("condition.cpp:1", result.stderr)
        self.assertIn("condition.cpp:2", result.stderr)
        self.assertIn("condition.cpp:3", result.stderr)
        self.assertIn("condition.cpp:4", result.stderr)
        self.assertIn("condition.cpp:5", result.stderr)

    def test_lambda_is_allowed_and_deleted_source_is_skipped(self):
        source = self.root / 'lambda café.cpp'
        subprocess.run(["git", "-C", str(self.root), "config", "core.quotePath", "true"], check=True)
        source.write_text("auto value = 42;\n")
        rejected = self.audit()
        self.assertEqual(rejected.returncode, 1)
        self.assertIn(source.name + ":1", rejected.stderr)
        source.write_text("auto fn = [](int n) { return n; };\n")
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        self.assertEqual(self.audit().returncode, 0)
        source.unlink()
        self.assertEqual(self.audit().returncode, 0)

    def test_nested_capture_brackets_are_allowed(self):
        (self.root / "nested.cpp").write_text('auto fn = [x = values[0], text = "]"]() { return x; };\n')
        self.assertEqual(self.audit().returncode, 0)

    def test_cpp23_lambda_specifiers_and_capture_literals_are_allowed(self):
        source = "auto fn = [n = 1'000, text = R\"tag(\" ] \")tag\"]() constexpr { return n; };\n"
        (self.root / "literal.cpp").write_text(source + "auto stateless = [] static { return 1; };\n")
        self.assertEqual(self.audit().returncode, 0)

    def test_immediately_invoked_lambdas_require_explicit_types(self):
        initializers = [
            "[]{ return 42; }()",
            "[] static { return 42; }()",
            "[] { return values.begin() != values.end(); }()",
            "[] { return values.begin(); }() == values.end()",
            "[] { return std::distance(values.begin(), values.end()); }()",
            "[](int n) constexpr noexcept -> int { return n; }(42)",
            "[n = values[0]] { return n; } /* call */ ()",
            "[]\n{\n    if (true) { return 42; }\n    return 0;\n}\n()",
            '[] { const char* text = "}();"; return 42; }()',
            '[] { const char* text = R"tag(}\n(); ")tag"; return 42; }()',
            "[] { /* } */ // }\n    return 42;\n}()",
            "[] { return [] { return 42; }(); }()",
        ]
        for initializer in initializers:
            with self.subTest(initializer=initializer):
                (self.root / "lambda.cpp").write_text(f"auto value = {initializer};\n")
                result = self.audit()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("lambda.cpp:1", result.stderr)

    def test_multiline_lambda_closures_preserve_body_literals_and_specifiers(self):
        initializers = [
            "[] static { return 42; }",
            "[]() mutable noexcept { return 42; }",
            "[]() consteval -> int { return 42; }",
            "[]<typename T>(T n) constexpr noexcept -> T { return n; }",
            "[]<int N = 1 << 2>() { return N; }",
            "[]<typename T>(T n) requires true && requires { n + n; } { return n; }",
            "[]<typename T> requires true (T n) requires true { return n; }",
            "[]<typename T = decltype([] { return 42; })>() { return T{}; }",
            "[]<typename T>(T n) requires requires { n + n; } { return n; }",
            "[]\n{\n    if (true) { return 42; }\n    return 0;\n}",
            '[text = R"tag(]\n\" [)tag"]() { return text; }',
            '[] { const char* text = "}();"; return text; }',
            '[] { const char* text = R"tag(}\n(); ")tag"; return text; }',
            "[] { /* } */ // }\n    return 1'000;\n}",
            "[] { return [] { return 42; }(); }",
        ]
        for initializer in initializers:
            with self.subTest(initializer=initializer):
                (self.root / "lambda.cpp").write_text(f"auto fn = {initializer};\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_direct_iterator_returns_from_invoked_lambdas_are_allowed(self):
        for expression in ["values.begin()", "(values.begin())", "std::begin(values)", "std::next(values.begin())"]:
            with self.subTest(expression=expression):
                (self.root / "iterator.cpp").write_text(
                    f"auto it = [&] {{ return {expression}; }}();\n"
                )
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_allowlist_regeneration_is_idempotent(self):
        (self.root / "value.cpp").write_text("auto value = 42;\n")
        self.assertEqual(self.audit("--write-allowlist").returncode, 0)
        first = (self.root / "tools/cpp_auto_allowlist.txt").read_text()
        self.assertIn("value.cpp|auto value = 42;", first)
        self.assertEqual(self.audit("--write-allowlist").returncode, 0)
        self.assertEqual((self.root / "tools/cpp_auto_allowlist.txt").read_text(), first)
        self.assertEqual(self.audit().returncode, 0)


if __name__ == "__main__":
    unittest.main()
