#!/usr/bin/env python3
import subprocess
from datetime import datetime

git_log = subprocess.check_output(['git', 'log', '-1', '--format=%ci']).decode().strip()
git_date = datetime.strptime(git_log, '%Y-%m-%d %H:%M:%S %z').strftime('%Y-%m-%d %H:%M')
git_hash = subprocess.check_output(['git', 'describe', '--always', '--tags', '--dirty']).decode().strip()

print(f'{git_date} {git_hash}', end='')
