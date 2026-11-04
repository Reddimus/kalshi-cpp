"""Run the release workflow's CI gate against a local GitHub CLI stub."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]


class ReleaseGateTests(unittest.TestCase):
    def run_gate(self, scenario):
        workflow = yaml.safe_load((ROOT / ".github/workflows/release.yml").read_text())
        gate = next(step["run"] for step in workflow["jobs"]["release"]["steps"] if step.get("name") == "Require passing CI on the tagged commit")
        with tempfile.TemporaryDirectory(prefix="kalshi-release-gate-") as temp:
            root = Path(temp)
            gh = root / "gh"
            gh.write_text('''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
args=sys.argv[1:]
marker=pathlib.Path(os.environ['DISPATCH_RECORD'])
if args[:2] == ['workflow','run']:
 marker.write_text(json.dumps(args))
 sys.exit(0)
if args[:2] != ['run','list']: sys.exit(2)
mode=os.environ['SCENARIO']
items=[] if mode=='empty' and not marker.exists() else [{'status':'completed','conclusion':'failure' if mode=='failure' else 'success'}]
sys.exit(subprocess.run(['jq','-r',args[args.index('--jq')+1]], input=json.dumps(items), text=True).returncode)
''')
            gh.chmod(0o700)
            sleep = root / "sleep"
            sleep.write_text("#!/bin/sh\nexit 0\n")
            sleep.chmod(0o700)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"], SCENARIO=scenario, DISPATCH_RECORD=str(root / "dispatch"), GITHUB_REPOSITORY="owner/repo", GITHUB_SHA="a" * 40, GITHUB_REF_NAME="v0.0.0")
            result = subprocess.run(["bash", "-c", gate], env=env, capture_output=True, text=True, timeout=30, check=False)
            dispatch = json.loads((root / "dispatch").read_text()) if (root / "dispatch").exists() else None
            return result, dispatch

    def test_missing_run_dispatches_ci_for_the_tag(self):
        result, dispatch = self.run_gate("empty")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(dispatch, ["workflow", "run", "ci.yml", "--repo", "owner/repo", "--ref", "v0.0.0"])

    def test_successful_run_does_not_dispatch(self):
        result, dispatch = self.run_gate("success")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNone(dispatch)

    def test_failed_ci_prevents_publication(self):
        result, dispatch = self.run_gate("failure")
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(dispatch)


if __name__ == "__main__":
    unittest.main()
