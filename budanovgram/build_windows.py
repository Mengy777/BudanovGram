import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--secrets', type=Path, default=Path('D:/Programs/BudanovGram/build-secrets.json'))
args = parser.parse_args()
if not args.secrets.is_file():
    sys.exit('Missing local build-secrets.json. See budanovgram/README-RU.md.')
try:
    secrets = json.loads(args.secrets.read_text(encoding='utf-8-sig'))
except (ValueError, OSError):
    sys.exit('Cannot read the local credentials JSON file.')
api_id = str(secrets.get('api_id', ''))
api_hash = str(secrets.get('api_hash', ''))
if not re.fullmatch(r'[1-9][0-9]*', api_id) or not re.fullmatch(r'[a-fA-F0-9]{32}', api_hash):
    sys.exit('Invalid api_id or api_hash format. Values were not printed.')
# CMake cache stays local and is excluded from Git.
initial_cache = root / 'out' / 'budanovgram-secrets.cmake'
initial_cache.parent.mkdir(exist_ok=True)
initial_cache.write_text(
    f'set(TDESKTOP_API_ID "{api_id}" CACHE STRING "")\n'
    f'set(TDESKTOP_API_HASH "{api_hash}" CACHE STRING "")\n', encoding='utf-8')
configure = ['python', 'configure.py', '-GNinja Multi-Config', '-C', str(initial_cache),
    '-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON']
if subprocess.run(configure, cwd=root / 'Telegram').returncode:
    sys.exit('Configuration failed. Credentials were not printed by this script.')
sys.exit(subprocess.run(['cmake', '--build', str(root / 'out'), '--config', 'Debug', '--target', 'Telegram']).returncode)
