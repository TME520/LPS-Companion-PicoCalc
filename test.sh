#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
mkdir -p build-host
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DLPS_TEST src/lps_companion.cpp -o build-host/lps_test
./build-host/lps_test
