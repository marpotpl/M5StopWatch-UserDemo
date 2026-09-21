#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/tesla-tests.XXXXXX)
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -Itests/tesla/stubs -Imain \
    tests/tesla/test_tesla.cpp main/hal/ha_tesla.cpp -o "$binary"
"$binary"
