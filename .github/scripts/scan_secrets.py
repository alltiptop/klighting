#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a trusted Gitleaks policy; publish metadata only, never scanner output."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def scan(source, policy, scanner, mode):
    source = source.resolve(strict=True)
    policy = policy.resolve(strict=True)
    if mode == 'git':
        metadata = source / '.git'
        if not metadata.is_dir() or not (metadata / 'HEAD').is_file():
            raise ValueError('A complete repository checkout is required')
        if (metadata / 'shallow').exists():
            raise ValueError('Shallow history cannot establish complete coverage')
    with tempfile.TemporaryDirectory(prefix='klighting-secret-scan-') as scratch:
        scratch = Path(scratch)
        raw = scratch / 'findings.json'
        # Ignore files and inline comments in the proposed source are untrusted.
        ignore = scratch / 'empty.ignore'
        ignore.touch()
        command = [scanner, mode, str(source), '--config', str(policy),
                   '--gitleaks-ignore-path', str(ignore), '--ignore-gitleaks-allow',
                   '--redact=100', '--no-banner', '--no-color', '--exit-code=10',
                   '--report-format=json', '--report-path', str(raw), '--timeout=300']
        if mode == 'git':
            # Scanning all fetched refs also catches secrets removed before HEAD.
            command.append('--log-opts=--all HEAD')
        env = {key: value for key, value in os.environ.items()
               if not key.startswith('GITLEAKS_')}
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                env=env, timeout=330, check=False)
        if result.returncode not in (0, 10):
            raise RuntimeError('Scanner execution failed')
        findings = json.loads(raw.read_text())
        if not isinstance(findings, list) or any(not isinstance(f, dict) for f in findings):
            raise ValueError('Invalid scanner report')
        if bool(findings) != (result.returncode == 10):
            raise ValueError('Scanner status does not match its report')
        safe = [{key: finding[key] for key in ('RuleID', 'File', 'StartLine', 'Commit')}
                for finding in findings]
        return {'status': 'findings' if safe else 'clean',
                'coverage': 'all fetched refs and HEAD' if mode == 'git' else 'directory',
                'findings': safe}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--policy', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--scanner', default='gitleaks')
    parser.add_argument('--mode', choices=('git', 'dir'), default='git')
    args = parser.parse_args()
    try:
        summary = scan(args.source, args.policy, args.scanner, args.mode)
        code = 1 if summary['findings'] else 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        # Exception text or raw scanner logs can contain the very secret being checked.
        summary = {'status': 'error', 'errorType': type(error).__name__}
        code = 2
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(summary, indent=2) + '\n')
    print('Secret scan:', summary['status'], '; findings:', len(summary.get('findings', [])))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
