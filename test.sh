#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
mkdir -p build-host
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DLPS_TEST src/lps_companion.cpp -o build-host/lps_test
./build-host/lps_test
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -Itests/stubs tests/storage_test.cpp -o build-host/storage_test
storage_test_path="$PWD/build-host/storage_test"
storage_test_dir="$(mktemp -d)"
trap 'rm -rf -- "$storage_test_dir"' EXIT
(cd "$storage_test_dir" && "$storage_test_path")
