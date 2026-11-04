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
