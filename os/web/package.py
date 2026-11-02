#!/usr/bin/env python3
"""Assemble only the engine, current ce.dat and explicitly listed app assets."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from import_ui import write_native_assets

ROOT = Path(__file__).resolve().parents[2]
SITE_FILES = ['index.html', 'app.css', 'app.js', 'filesystem.js', 'browser.js',
              'app-cache.js', 'audio-worklet.js', 'install-hint.js', 'problem-view.js', 'visits.js', 'privacy.html', 'privacy-page.js', 'strings.js', 'resource-store.js', 'working-files.js', 'storage-common.js', 'user-report.js',
              'zip-import.js', 'probe-worker.js', 'manifest.webmanifest',
              'game-config.js', 'files.html', 'files.css', 'files.js', 'files-view.js', 'files-page.js',
              'save-session.js', 'save-download.js',
              'user-store.js', 'user-lazy-filesystem.js', 'user-runtime.js',
              'save-format.js', 'save-streams.js', 'save-zip-reader.js', 'zip-directory.js',
              'zip-output.js', 'opfs-export.js',
              'icons/apple-touch-icon.png', 'icons/icon-192.png', 'icons/icon-512.png',
              'vendor/zipjs/zip.min.js', 'vendor/zipjs/LICENSE', 'vendor/zipjs/README.md']
FONT_FILES = ['FalloutClassicDialog.ttf', 'FalloutClassicDialog-LICENSE.txt', 'PTM55FT.ttf', 'OFL.txt']
NATIVE_FILES = ['import-native.js', 'import-native.css', 'icons/import-back.svg']


def git(*args):
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ''


# sw.js is generated from this template with every other file's hash.
SERVICE_WORKER = 'sw.js'
BUILD_MARKER = '__FALLOUT_BUILD__'


def service_worker(template, build_id, files):
    if template.count(BUILD_MARKER) != 1:
        raise ValueError('The service worker template needs one build marker')
    build = json.dumps({'buildId': build_id, 'files': files}, sort_keys=True, separators=(',', ':'))
    return template.replace(BUILD_MARKER, build)


def seal(stage, app_version, configuration):
    """List the staged files, add sw.js and build-info.json, then verify."""
    files = {}
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            data = path.read_bytes()
            files[path.relative_to(stage).as_posix()] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    # Include frontend assets: a keyboard/CSS-only fix must be identifiable
    # on a tester's phone even when the WASM binary has not changed. The
    # worker template counts too: a change to it is a new build.
    template = (ROOT / 'os/web' / SERVICE_WORKER).read_text()
    build_id = hashlib.sha256((json.dumps(files, sort_keys=True) + template).encode()).hexdigest()[:8]
    worker = service_worker(template, build_id, files).encode()
    (stage / SERVICE_WORKER).write_bytes(worker)
    files[SERVICE_WORKER] = {'bytes': len(worker), 'sha256': hashlib.sha256(worker).hexdigest()}
    info = {'schema': 1, 'version': app_version, 'buildId': build_id,
            'source': {'commit': git('rev-parse', 'HEAD'), 'dirty': bool(git('status', '--porcelain'))},
            'toolchain': {'emscripten': (ROOT / 'os/web/emscripten-version.txt').read_text().strip(),
                          'configuration': configuration, 'wasm': 32, 'asyncify': True},
            'files': files}
    (stage / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n')
    return verify(stage)


def verify(directory):
    info = json.loads((directory / 'build-info.json').read_text())
    if info['schema'] != 1 or not info['version']:
        raise ValueError('Invalid build metadata')
    files = info['files']
    for name in ['fallout2-ce.js', 'fallout2-ce.wasm', 'fallout2-archive.js', 'fallout2-archive.wasm',
                 'ce.dat', 'LICENSE.md', 'licenses/libarchive.txt', SERVICE_WORKER, *SITE_FILES, *NATIVE_FILES,
                 *['fonts/' + font for font in FONT_FILES]]:
        if name not in files:
            raise ValueError('Missing app asset: ' + name)
    if (directory / 'fallout2-ce.wasm').read_bytes()[:4] != b'\0asm':
        raise ValueError('Not a WASM module')
    if (directory / 'fallout2-archive.wasm').read_bytes()[:4] != b'\0asm':
        raise ValueError('Not an archive WASM module')
    actual = {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file()}
    if actual != set(files) | {'build-info.json'}:
        raise ValueError('Missing or unlisted files in app output')
    for name, expected in files.items():
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError('Invalid app asset path: ' + name)
        if Path(name).name.lower() in ('master.dat', 'critter.dat'):
            raise ValueError('Original game data in output')
        data = (directory / name).read_bytes()
        if len(data) != expected['bytes']:
            raise ValueError('Wrong size: ' + name)
        if hashlib.sha256(data).hexdigest() != expected['sha256']:
            raise ValueError('Wrong hash: ' + name)
    # The worker caches exactly these files; a stale copy would serve another build.
    match = re.search(r'^const BUILD = (\{.*\});$', (directory / SERVICE_WORKER).read_text(), re.M)
    embedded = json.loads(match[1]) if match else None
    if not embedded or embedded['buildId'] != info['buildId'] or embedded['files'] != {
            name: value for name, value in files.items() if name != SERVICE_WORKER}:
        raise ValueError('The service worker does not match build-info.json')
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', type=Path)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--dat-tool', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--app-version')
    parser.add_argument('--configuration', default='Release')
    args = parser.parse_args()
    if args.verify:
        info = verify(args.verify.resolve())
        print('Verified ' + info['version'] + ': ' + str(len(info['files'])) + ' assets')
        return
    if not all([args.engine, args.archive, args.dat_tool, args.output, args.app_version]):
        parser.error('Packaging requires --engine, --archive, --dat-tool, --output, --app-version')
    output = args.output.resolve()
    if output == ROOT or output in ROOT.parents:
        parser.error('Output must not replace the source tree or its parents')
    if output.exists() and (not output.is_dir() or (any(output.iterdir()) and not (output / 'build-info.json').is_file())):
        parser.error('Refusing to replace a nonempty directory that is not a web bundle')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.web-package-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'dist'
        stage.mkdir()
        for name in SITE_FILES:
            (stage / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / 'os/web' / name, stage / name)
        write_native_assets(stage)
        for name in ['fallout2-ce.js', 'fallout2-ce.wasm']:
            shutil.copyfile(args.engine.parent / name, stage / name)
        for name in ['fallout2-archive.js', 'fallout2-archive.wasm']:
            shutil.copyfile(args.archive.parent / name, stage / name)
        (stage / 'licenses').mkdir()
        shutil.copyfile(ROOT / 'files/ce.dat/licenses/libarchive.txt',stage / 'licenses/libarchive.txt')
        (stage / 'fonts').mkdir()
        for name in FONT_FILES:
            shutil.copyfile(ROOT / 'files/ce.dat/fonts' / name, stage / 'fonts' / name)
        shutil.copyfile(ROOT / 'LICENSE.md', stage / 'LICENSE.md')
        subprocess.run([str(args.dat_tool.resolve()), 'create', str(ROOT / 'files/ce.dat'), str(stage / 'ce.dat')], check=True)
        info = seal(stage, args.app_version, args.configuration)
        previous = Path(temporary) / 'previous'
        if output.exists():
            output.rename(previous)
        try:
            stage.rename(output)
        except OSError:
            if previous.exists():
                previous.rename(output)
            raise
    print('Packaged ' + args.app_version + ': ' + str(sum(f['bytes'] for f in info['files'].values())) + ' bytes')


if __name__ == '__main__':
    main()
