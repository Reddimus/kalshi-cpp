"""Check that every dependency CMake downloads is pinned to exact bytes."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECLARATION = re.compile(r"FetchContent_Declare\(\s*(\w+)(.*?)\)", re.DOTALL)


def declarations():
    """Each FetchContent_Declare in the build files, as (file, name, arguments)."""
    found = []
    for path in sorted([ROOT / "CMakeLists.txt", *ROOT.glob("cmake/*.cmake"), *ROOT.glob("*/CMakeLists.txt")]):
        for match in DECLARATION.finditer(path.read_text()):
            found.append((path.relative_to(ROOT).as_posix(), match.group(1), match.group(2).split()))
    return found


class DependencyPinTests(unittest.TestCase):
    def test_the_build_downloads_the_dependencies_it_documents(self):
        self.assertEqual(sorted(name for _, name, _ in declarations()), ["benchmark", "glaze", "googletest"])

    def test_downloads_are_https_archives_with_a_sha256(self):
        # A git tag can be moved to another commit. A hash of the archive cannot
        # follow it: the download fails instead of building something else.
        for path, name, arguments in declarations():
            with self.subTest(dependency=name, file=path):
                options = dict(zip(arguments[::2], arguments[1::2]))
                self.assertEqual(sorted(options), ["URL", "URL_HASH"], "expected only URL and URL_HASH")
                self.assertRegex(options["URL"], r"^https://github\.com/[\w.-]+/[\w.-]+/archive/refs/tags/v[\d.]+\.tar\.gz$")
                self.assertRegex(options["URL_HASH"], r"^SHA256=[0-9a-f]{64}$")

    def test_each_pin_says_which_version_and_commit_it_is(self):
        text = (ROOT / "CMakeLists.txt").read_text()
        for _, name, arguments in declarations():
            with self.subTest(dependency=name):
                version = re.search(r"/(v[\d.]+)\.tar\.gz$", dict(zip(arguments[::2], arguments[1::2])).get("URL", "")).group(1)
                before = text[: text.index("FetchContent_Declare(" + name)].splitlines()[-6:]
                comment = " ".join(line.strip().lstrip("# ") for line in before if line.strip().startswith("#"))
                self.assertRegex(comment, re.escape(version) + r" \(commit [0-9a-f]{40}\)")


if __name__ == "__main__":
    unittest.main()
