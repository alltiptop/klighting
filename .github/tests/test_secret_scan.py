# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

POLICY_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = POLICY_ROOT / 'scripts' / 'scan_secrets.py'


class SecretScanTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.report = self.root / 'summary.json'
        self.policy = self.root / 'policy.toml'
        shutil.copyfile(POLICY_ROOT / 'gitleaks.toml', self.policy)

    def run_scan(self, *extra):
        command = [sys.executable, str(SCRIPT), str(self.source), '--policy', str(self.policy),
                   '--report', str(self.report), '--mode=dir', *extra]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20, check=False)
        return result, json.loads(self.report.read_text())

    def test_clean_source(self):
        (self.source / 'example.txt').write_text('A public example without credentials.\n')
        result, report = self.run_scan()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(report['status'], 'clean')

    def test_secret_is_detected_and_never_published(self):
        token = 'glpat-' + 'z9A2k8Q4m7R1x6W3p5N0'
        (self.source / 'example.txt').write_text('token = "' + token + '" # gitleaks:allow\n')
        # Proposed local policy/ignore files must not override the trusted policy.
        (self.source / '.gitleaks.toml').write_text('[allowlist]\npaths = [".*"]\n')
        (self.source / '.gitleaksignore').write_text('example.txt:gitlab-pat:1\n')
        result, report = self.run_scan()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(report['status'], 'findings')
        self.assertNotIn(token, result.stdout + result.stderr + self.report.read_text())
        self.assertTrue(report['findings'])
        self.assertTrue(all(set(f) == {'RuleID', 'File', 'StartLine', 'Commit'}
                            for f in report['findings']))

    def test_reviewed_false_positive_is_narrow(self):
        token = 'glpat-' + 'z9A2k8Q4m7R1x6W3p5N0'
        self.policy.write_text(self.policy.read_text() + f'''
[[allowlists]]
description = "Synthetic public fixture only"
targetRules = ["gitlab-pat"]
condition = "AND"
paths = ['(^|/)public-example[.]txt$']
regexTarget = "secret"
regexes = ['^{token}$']
''')
        (self.source / 'public-example.txt').write_text(token)
        result, report = self.run_scan()
        self.assertEqual(result.returncode, 0)
        another_token = 'glpat-' + 'r8B3j7P5n6T2w9Y4q1M0'
        (self.source / 'public-example.txt').write_text(token + '\n' + another_token + '\n')
        result, report = self.run_scan()
        self.assertEqual(result.returncode, 1)
        self.assertTrue(any(f['File'].endswith('public-example.txt') and f['StartLine'] == 2
                            for f in report['findings']))
        self.assertNotIn(another_token, result.stdout + result.stderr + self.report.read_text())
        (self.source / 'public-example.txt').write_text(token)
        (self.source / 'private.txt').write_text(token)
        result, report = self.run_scan()
        self.assertEqual(result.returncode, 1)
        self.assertTrue(all(f['File'].endswith('private.txt') for f in report['findings']))

    def test_scanner_failure_is_not_clean(self):
        result, report = self.run_scan('--scanner=/nonexistent/klighting-scanner')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(report['status'], 'error')

    def test_shallow_history_is_rejected_without_running_git(self):
        metadata = self.source / '.git'
        metadata.mkdir()
        (metadata / 'HEAD').write_text('ref: refs/heads/main\n')
        (metadata / 'shallow').write_text('0' * 40 + '\n')
        result, report = self.run_scan('--mode=git')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(report['status'], 'error')


if __name__ == '__main__':
    unittest.main()
