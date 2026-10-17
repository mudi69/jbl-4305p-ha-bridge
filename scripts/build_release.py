"""Build credential-free example firmware in an isolated directory; never uploads."""

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

import yaml

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, default=ROOT / '.build-release',
                        help='Use a short ASCII directory on Windows')
    parser.add_argument('--output', type=Path, default=ROOT / 'releases/2026.10.17')
    args = parser.parse_args()
    if importlib.metadata.version('esphome') != '2026.9.1':
        parser.error('Use requirements-build.txt: ESPHome 2026.9.1 is required')
    # A new directory prevents accidental reuse of personal binaries or secrets.
    work = args.build_root.resolve() / ('example-' + uuid.uuid4().hex[:12])
    work.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'firmware/esphome/jbl-4305p-bridge.yaml'
    inputs = [source, ROOT / 'build/public-example.secrets.yaml', ROOT / 'requirements-build.txt',
              ROOT / 'jbl_protocol.json', Path(__file__).resolve()]
    inputs += [p for p in (ROOT / 'firmware/esphome/components').rglob('*')
               if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc']
    input_hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): digest(p) for p in sorted(inputs)}
    config = work / source.name
    content = source.read_text(encoding='utf-8')
    if 'build_path:' in content:
        raise ValueError('Canonical YAML must not contain a machine-specific build_path')
    content = content.replace('  min_version: 2026.9.1',
                              '  min_version: 2026.9.1\n  build_path: ' + json.dumps(str(work / 'build').replace('\\', '/')), 1)
    config.write_text(content, encoding='utf-8')
    shutil.copytree(ROOT / 'firmware/esphome/components', work / 'components',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    shutil.copyfile(ROOT / 'build/public-example.secrets.yaml', work / 'secrets.yaml')
    for original in inputs:
        relative = original.relative_to(ROOT).as_posix()
        if relative.startswith('firmware/esphome/components/'):
            copied = work / original.relative_to(ROOT / 'firmware/esphome')
            if digest(copied) != input_hashes[relative]:
                raise RuntimeError(f'Source changed during snapshot: {relative}')
    if digest(work / 'secrets.yaml') != input_hashes['build/public-example.secrets.yaml']:
        raise RuntimeError('Public configuration changed during snapshot')
    if hashlib.sha256(source.read_bytes()).hexdigest() != input_hashes[source.relative_to(ROOT).as_posix()]:
        raise RuntimeError('Canonical YAML changed during snapshot')
    print(f'Isolated public-example build: {work}', flush=True)
    subprocess.run([sys.executable, '-m', 'esphome', 'compile', str(config)], check=True)
    for name, expected in input_hashes.items():
        if digest(ROOT / name) != expected:
            raise RuntimeError(f'Source changed during build; rerun before release: {name}')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = work / 'build/build'
    for name in ('firmware.factory.bin', 'firmware.ota.bin'):
        shutil.copyfile(binaries / name, output / name)
    shutil.copyfile(work / 'build/partitions.csv', output / 'partitions.csv')
    shutil.copyfile(ROOT / 'build/public-example.secrets.yaml', output / 'public-example.secrets.yaml')
    shutil.copyfile(work / 'build/sdkconfig.jbl-4305p-bridge', output / 'sdkconfig.example')
    dependencies = yaml.safe_load((work / 'build/dependencies.lock').read_text(encoding='utf-8'))
    # Cache paths are machine-specific, not inputs another user can resolve.
    def portable(value):
        if isinstance(value, dict):
            return {k: ('<resolved-by-pinned-esphome>' if k in ('path', 'override_path') else portable(v))
                    for k, v in value.items()}
        if isinstance(value, list):
            return [portable(v) for v in value]
        return value
    (output / 'dependencies.resolved.json').write_text(
        json.dumps(portable(dependencies), indent=2) + '\n', encoding='utf-8')
    flash = json.loads((binaries / 'flasher_args.json').read_text(encoding='utf-8'))
    build_info = json.loads((work / 'build/build_info.json').read_text(encoding='utf-8'))
    manifest = {
        'release': '2026.10.17', 'generated_at_utc': datetime.now(timezone.utc).isoformat(),
        'profile': 'PUBLIC EXAMPLE ONLY - recompile with private installation settings',
        'esphome_version': '2026.9.1', 'esp_idf_version': '5.5.5',
        'python_version': sys.version.split()[0], 'board': 'esp32-c3-devkitm-1',
        'build': build_info, 'flash_settings': flash['flash_settings'],
        'factory_flash_offset': '0x0', 'application_flash_offset': flash['app']['offset'],
        'source_config': 'firmware/esphome/jbl-4305p-bridge.yaml',
        'public_secrets': 'build/public-example.secrets.yaml',
        'rebuild': 'python scripts/build_release.py --build-root <ASCII_BUILD_DIRECTORY>',
        'configuration_note': 'Only build_path is added to an isolated copy; all firmware/runtime inputs are listed below.',
        'inputs_sha256': input_hashes,
        'artifacts': {name: {'bytes': (output / name).stat().st_size, 'sha256': digest(output / name)}
                      for name in ('firmware.factory.bin', 'firmware.ota.bin', 'partitions.csv', 'public-example.secrets.yaml',
                                   'sdkconfig.example', 'dependencies.resolved.json')},
        'validation': {'compiled': True, 'parameterized_firmware_hardware_tested': False,
                       'second_hardware_pair_tested': False, 'production_device_updated': False},
        'limits': ['No runtime provisioning portal', 'Public example credentials must never be used in production',
                   'Do not OTA this demo onto an existing installation',
                   'Personalized firmware and full-flash/NVS backups must remain private',
                   'Rebuild process is reproducible; timestamps mean binary identity across builds is not promised']
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    names = list(manifest['artifacts']) + ['manifest.json']
    (output / 'SHA256SUMS').write_text(''.join(f'{digest(output / name)}  {name}\n' for name in names), encoding='utf-8')
    print(json.dumps({'release': str(output), 'artifacts': manifest['artifacts']}, indent=2), flush=True)


if __name__ == '__main__':
    main()
