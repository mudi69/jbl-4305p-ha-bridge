"""Check staged Git contents and release hashes without printing credentials."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

import yaml

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--private-secrets', type=Path, default=ROOT / 'firmware/esphome/secrets.yaml')
    parser.add_argument('--allow-no-private-baseline', action='store_true',
                        help='For a fresh public clone only; cannot detect unknown private values')
    args = parser.parse_args()
    files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    files = [name for name in files if name]
    if not files:
        raise SystemExit('Nothing staged; git add the curated public files first.')
    needles = []
    if not args.private_secrets.is_file() and not args.allow_no_private_baseline:
        raise SystemExit('Private baseline missing. Supply --private-secrets, or explicitly use --allow-no-private-baseline for a public clone.')
    if args.private_secrets.is_file():
        private = yaml.safe_load(args.private_secrets.read_text(encoding='utf-8-sig'))
        for key, value in private.items():
            if not isinstance(value, str) or len(value) < 6:
                continue
            needles.append((key, value.encode()))
            if key.endswith('_api_key'):
                needles.append((key + ':decoded', base64.b64decode(value, validate=True)))
            if key.endswith('_mac'):
                needles.append((key + ':compact', value.replace(':', '').encode()))
                needles.append((key + ':bytes', bytes.fromhex(value.replace(':', ''))))
    findings = []
    def staged(name):
        if name not in files:
            raise SystemExit(f'Required release file is not staged: {name}')
        return subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)
    forbidden = {'captures', 'tools', '.local', '.build-release', '__pycache__', '.esphome'}
    def inspect(name, data):
        path = Path(name)
        if any(p in forbidden or p.startswith('.venv') for p in path.parts) or path.name == 'secrets.yaml':
            findings.append((name, 'private/cache path'))
        if path.suffix in {'.pcap', '.pcapng', '.elf', '.map', '.log'}:
            findings.append((name, 'raw capture/build/debug artifact'))
        if path.suffix.lower() in {'.zip', '.tar', '.gz', '.tgz', '.bz2', '.xz', '.7z', '.rar'}:
            findings.append((name, 'archive requires separate expanded-content audit'))
        if path.suffix == '.bin' and not (path.parts[0] == 'releases' or name == 'evidence/hid-report-map.bin'):
            findings.append((name, 'uncurated binary'))
        for label, needle in needles:
            if needle and (needle in data or (label.endswith(':compact') and needle.lower() in data.lower())):
                findings.append((name, 'private value matching ' + label))
        if re.search(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----)', data):
            findings.append((name, 'credential pattern'))
        if re.search(rb'[A-Za-z]:[/\\]+Users[/\\]+[A-Za-z0-9_-]+', data):
            findings.append((name, 'personal absolute path'))
    for name in files:
        inspect(name, staged(name))
    # Scan publishable Git history as well as the index. Desktop recovery refs
    # are private snapshots, not branch/tag history; never push --mirror or them.
    history = subprocess.check_output(['git', 'rev-list', '--objects', '--branches', '--tags', '--remotes'], cwd=ROOT).decode().splitlines()
    history_blobs = 0
    for line in history:
        oid, _, name = line.partition(' ')
        kind = subprocess.check_output(['git', 'cat-file', '-t', oid], cwd=ROOT).strip()
        if kind == b'blob':
            history_blobs += 1
            inspect(name or oid, subprocess.check_output(['git', 'cat-file', 'blob', oid], cwd=ROOT))
    release = 'releases/2026.10.17/'
    manifest = json.loads(staged(release + 'manifest.json'))
    for name, expected in manifest['inputs_sha256'].items():
        if hashlib.sha256(staged(name)).hexdigest() != expected:
            findings.append((name, 'source differs from released build input'))
    for line in staged(release + 'SHA256SUMS').decode().splitlines():
        expected, name = line.split('  ', 1)
        if hashlib.sha256(staged(release + name)).hexdigest() != expected:
            findings.append((name, 'release hash mismatch'))
    if findings:
        for name, reason in findings:
            print(f'FAIL {name}: {reason}')
        raise SystemExit(1)
    print(json.dumps({'status': 'PASS', 'staged_files': len(files),
                      'historical_blobs': history_blobs,
                      'history_scope': 'all local branches, tags and remote-tracking branches; never push private recovery refs',
                      'private_value_scan': bool(needles), 'release_and_source_hashes': 'PASS'}))


if __name__ == '__main__':
    main()
