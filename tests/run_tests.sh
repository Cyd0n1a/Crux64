#!/usr/bin/env bash
# Host-side unit tests. These do NOT need Docker or the N64 toolchain —
# every file under test is deliberately free of libdragon.
set -e
cd "$(dirname "$0")"
mkdir -p build
gcc -std=c99 -O1 -Wall -Wextra -Werror \
    -o build/menu_nav_test menu_nav_test.c ../src/meta/menu_nav.c -lm
./build/menu_nav_test
