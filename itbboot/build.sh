#!/usr/bin/env bash
# Build libitbboot.so - the LD_PRELOAD bootstrap for package.loadlib injection
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Compile the bootstrap with -shared -fPIC -O2 -ldl
cc -shared -fPIC -O2 -o libitbboot.so itbboot.c -ldl

echo "Built libitbboot.so"
ls -lh libitbboot.so
