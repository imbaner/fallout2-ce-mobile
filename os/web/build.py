#!/usr/bin/env python3
"""Build the native DAT tool, then the current source tree for the browser."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PORTS_DIR = '/tmp/f2touch-emscripten-ports'


def run(command, env=None):
    print('+ ' + ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=ROOT, env=env, check=True)


def version():
    result = subprocess.run(['git', 'describe', '--tags', '--always', '--dirty'],
                            cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else 'development'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emsdk', type=Path, default=os.environ.get('EMSDK'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'out/web')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dat-tool', type=Path, help='Reuse an existing native ce-dat-tool')
    parser.add_argument('--app-version', default=None)
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument('--diagnostics', action='store_true')
    args = parser.parse_args()
    if sys.version_info < (3, 10):
        parser.error('Use Python 3.10 or newer (the Emscripten toolchain needs it).')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    app_version = args.app_version or version()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.+_-]{0,95}', app_version):
        parser.error('--app-version must be a short version identifier')

    build = args.build_dir.resolve()
    output = (args.output or build / 'dist').resolve()
    env = os.environ.copy()
    env['EMSDK_PYTHON'] = sys.executable
    # Emscripten builds its ports (SDL2, SDL2_mixer, zlib) from sources in its
    # ports directory, without our flags, so their __FILE__ is that path. A
    # fixed one outside the builder's home keeps local paths out of the app's
    # files and the engine the same on every machine; the build's own cache
    # has the ports built from there.
    env['EM_PORTS'] = PORTS_DIR
    env['EM_CACHE'] = str(build / 'emscripten-cache')
    if args.emsdk:
        sdk = args.emsdk.resolve()
        env['EMSDK'] = str(sdk)
        env['EM_CONFIG'] = str(sdk / '.emscripten')
        env['PATH'] = str(sdk / 'upstream/emscripten') + os.pathsep + env.get('PATH', '')
    emcc = shutil.which('emcc', path=env.get('PATH'))
    emcmake = shutil.which('emcmake', path=env.get('PATH'))
    if not emcc or not emcmake:
        parser.error('Activate Emscripten 4.0.4 or pass --emsdk /path/to/emsdk. See os/web/README.md.')
    required = (ROOT / 'os/web/emscripten-version.txt').read_text().strip()
    compiler = subprocess.check_output([emcc, '--version'], env=env, text=True)
    if not re.search(r'\b' + re.escape(required) + r'\b', compiler.splitlines()[0]):
        parser.error('Expected Emscripten ' + required + '; found ' + compiler.splitlines()[0])

    build.mkdir(parents=True, exist_ok=True)
    dat_tool = args.dat_tool.resolve() if args.dat_tool else build / 'native/ce-dat-tool'
    if not args.dat_tool:
        run(['cmake', '-S', ROOT, '-B', build / 'native', '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DFALLOUT_VENDORED=ON'])
        run(['cmake', '--build', build / 'native', '--target', 'ce-dat-tool', '--parallel', args.jobs])
    if not dat_tool.is_file():
        parser.error('Native ce-dat-tool not found: ' + str(dat_tool))

    run([emcmake, 'cmake', '-S', ROOT, '-B', build / 'engine', '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DPython3_EXECUTABLE=' + sys.executable,
         '-DFALLOUT_WEB_DAT_TOOL=' + str(dat_tool), '-DFALLOUT_WEB_DIST_DIR=' + str(output),
         '-DFALLOUT_APP_VERSION=' + app_version,
         '-DFALLOUT_DIAGNOSTICS=' + ('ON' if args.diagnostics else 'OFF')], env)
    run(['cmake', '--build', build / 'engine', '--target', 'web-dist', '--parallel', args.jobs], env)
    print('Web app: ' + str(output))


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
