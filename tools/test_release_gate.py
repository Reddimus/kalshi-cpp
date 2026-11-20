"""Run the release workflow's CI gate against a local GitHub CLI stub."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
SHA = "a" * 40
DISPATCH = ["workflow", "run", "ci.yml", "--repo", "owner/repo", "--ref", "v0.0.0"]

# Answers `gh run list` from the scenario file and records `gh workflow run`.
# Each poll takes the next list of runs, and the last list repeats. Polls made
# after a dispatch read "after_dispatch" instead. Like the real CLI, it prints
# newest first and only the fields named by --json.
GH_STUB = '''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
args = sys.argv[1:]
state = pathlib.Path(os.environ["GATE_STATE"])
scenario = json.loads((state / "scenario.json").read_text())
dispatches = state / "dispatches.json"
if args[:2] == ["workflow", "run"]:
    recorded = json.loads(dispatches.read_text()) if dispatches.exists() else []
    dispatches.write_text(json.dumps(recorded + [args]))
    sys.exit(0)
if args[:2] != ["run", "list"]:
    sys.exit(2)
for flag, value in (("--workflow", "ci.yml"), ("--commit", os.environ["GITHUB_SHA"])):
    if flag not in args or args[args.index(flag) + 1] != value:
        sys.exit(f"gh run list needs {flag} {value}")
phase = "after_dispatch" if dispatches.exists() else "polls"
counter = state / (phase + ".count")
poll = int(counter.read_text()) if counter.exists() else 0
counter.write_text(str(poll + 1))
answers = scenario.get(phase) or [[]]
runs = sorted(answers[min(poll, len(answers) - 1)], key=lambda run: run["createdAt"], reverse=True)
fields = args[args.index("--json") + 1].split(",")
text = json.dumps([{field: run[field] for field in fields} for run in runs])
if "--jq" in args:
    sys.exit(subprocess.run(["jq", "-r", args[args.index("--jq") + 1]], input=text, text=True).returncode)
print(text)
'''


def run(run_id, event, status, conclusion="", *, sha=SHA, created=None):
    """One CI run as `gh run list --json` reports it."""
    return {
        "databaseId": run_id,
        "event": event,
        "headSha": sha,
        "status": status,
        "conclusion": conclusion,
        "createdAt": created or f"2026-01-01T00:{run_id:02d}:00Z",
    }


class ReleaseGateTests(unittest.TestCase):
    def run_gate(self, polls, after_dispatch=None):
        workflow = yaml.safe_load((ROOT / ".github/workflows/release.yml").read_text())
        gate = next(step["run"] for step in workflow["jobs"]["release"]["steps"] if step.get("name") == "Require passing CI on the tagged commit")
        with tempfile.TemporaryDirectory(prefix="kalshi-release-gate-") as temp:
            root = Path(temp)
            (root / "scenario.json").write_text(json.dumps({"polls": polls, "after_dispatch": after_dispatch}))
            gh = root / "gh"
            gh.write_text(GH_STUB)
            gh.chmod(0o700)
            sleep = root / "sleep"
            sleep.write_text("#!/bin/sh\nexit 0\n")
            sleep.chmod(0o700)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"], GATE_STATE=str(root), GITHUB_REPOSITORY="owner/repo", GITHUB_SHA=SHA, GITHUB_REF_NAME="v0.0.0")
            # GitHub runs a step's script with `bash -e`.
            result = subprocess.run(["bash", "-e", "-c", gate], env=env, capture_output=True, text=True, timeout=120, check=False)
            dispatches = json.loads((root / "dispatches.json").read_text()) if (root / "dispatches.json").exists() else []
            return result, dispatches

    def assert_passes(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_blocks(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("::error::", result.stdout)

    def test_missing_run_dispatches_ci_for_the_tag(self):
        result, dispatches = self.run_gate([[]], after_dispatch=[[], [run(1, "workflow_dispatch", "queued")], [run(1, "workflow_dispatch", "completed", "success")]])
        self.assert_passes(result)
        self.assertEqual(dispatches, [DISPATCH])

    def test_successful_push_run_does_not_dispatch(self):
        result, dispatches = self.run_gate([[run(1, "push", "completed", "success")]])
        self.assert_passes(result)
        self.assertEqual(dispatches, [])

    def test_successful_manual_run_does_not_dispatch(self):
        result, dispatches = self.run_gate([[run(1, "workflow_dispatch", "completed", "success")]])
        self.assert_passes(result)
        self.assertEqual(dispatches, [])

    def test_waits_for_a_run_in_progress(self):
        result, dispatches = self.run_gate([[run(1, "push", "queued")], [run(1, "push", "in_progress")], [run(1, "push", "completed", "success")]])
        self.assert_passes(result)
        self.assertEqual(dispatches, [])

    def test_failed_ci_prevents_publication(self):
        for conclusion in ("failure", "timed_out", "startup_failure", "skipped", "action_required"):
            with self.subTest(conclusion=conclusion):
                result, dispatches = self.run_gate([[run(1, "push", "completed", conclusion)]])
                self.assert_blocks(result)
                self.assertEqual(dispatches, [])

    def test_pull_request_run_is_not_the_tagged_tree(self):
        # A pull_request run is listed under the head commit but tests a merge commit.
        result, dispatches = self.run_gate(
            [[run(1, "pull_request", "completed", "success")]],
            after_dispatch=[[run(1, "pull_request", "completed", "success"), run(2, "workflow_dispatch", "completed", "failure")]],
        )
        self.assert_blocks(result)
        self.assertEqual(dispatches, [DISPATCH])

    def test_newer_pull_request_success_does_not_hide_a_failed_push(self):
        result, dispatches = self.run_gate([[run(1, "push", "completed", "failure"), run(2, "pull_request", "completed", "success"), run(3, "pull_request_target", "completed", "success"), run(4, "merge_group", "completed", "success")]])
        self.assert_blocks(result)
        self.assertEqual(dispatches, [])

    def test_run_for_another_commit_does_not_count(self):
        result, dispatches = self.run_gate(
            [[run(1, "push", "completed", "success", sha="b" * 40)]],
            after_dispatch=[[run(1, "push", "completed", "success", sha="b" * 40), run(2, "workflow_dispatch", "completed", "success")]],
        )
        self.assert_passes(result)
        self.assertEqual(dispatches, [DISPATCH])

    def test_newest_qualifying_run_decides(self):
        result, dispatches = self.run_gate([[run(1, "push", "completed", "failure"), run(2, "workflow_dispatch", "completed", "success")]])
        self.assert_passes(result)
        self.assertEqual(dispatches, [])
        result, dispatches = self.run_gate([[run(1, "push", "completed", "success"), run(2, "workflow_dispatch", "completed", "failure")]])
        self.assert_blocks(result)

    def test_cancelled_run_is_started_again_once(self):
        cancelled = run(1, "push", "completed", "cancelled")
        # The new run takes a moment to be listed, so the cancelled one is seen again.
        result, dispatches = self.run_gate(
            [[cancelled]],
            after_dispatch=[[cancelled], [cancelled], [cancelled, run(2, "workflow_dispatch", "in_progress")], [cancelled, run(2, "workflow_dispatch", "completed", "success")]],
        )
        self.assert_passes(result)
        self.assertEqual(dispatches, [DISPATCH])

    def test_cancelled_again_prevents_publication(self):
        cancelled = run(1, "push", "completed", "cancelled")
        result, dispatches = self.run_gate([[cancelled]], after_dispatch=[[cancelled, run(2, "workflow_dispatch", "completed", "cancelled")]])
        self.assert_blocks(result)
        self.assertEqual(dispatches, [DISPATCH])

    def test_dispatched_run_that_fails_prevents_publication(self):
        result, dispatches = self.run_gate([[]], after_dispatch=[[run(1, "workflow_dispatch", "completed", "failure")]])
        self.assert_blocks(result)
        self.assertEqual(dispatches, [DISPATCH])

    def test_times_out_when_ci_never_finishes(self):
        result, dispatches = self.run_gate([[run(1, "push", "in_progress")]])
        self.assert_blocks(result)
        self.assertIn("timed out", result.stdout)
        self.assertEqual(dispatches, [])

    def test_cancelled_run_that_is_never_replaced_times_out(self):
        cancelled = run(1, "push", "completed", "cancelled")
        result, dispatches = self.run_gate([[cancelled]], after_dispatch=[[cancelled]])
        self.assert_blocks(result)
        self.assertEqual(dispatches, [DISPATCH])


if __name__ == "__main__":
    unittest.main()
