#!/bin/sh
set -eu

program=$1
check_output=$2
temp=$(mktemp -d "$(dirname "$program")/cli-test-XXXXXX")
trap 'rm -rf "$temp"' EXIT HUP INT TERM

expect_error() {
    if "$program" "$@" >"$temp/stdout" 2>"$temp/stderr"; then
        echo "expected failure: $*" >&2
        exit 1
    else
        status=$?
    fi
    if [ "$status" -ne 1 ] || [ ! -s "$temp/stderr" ]; then
        echo "expected exit 1 and an error message: $* (got $status)" >&2
        cat "$temp/stderr" >&2
        exit 1
    fi
}

"$program" --help >"$temp/help"
grep -q 'Usage:' "$temp/help"
for filter in identity blur gaussian sharpen edge; do
    "$program" tests/impulse.png "$filter" "$temp/$filter.png"
    "$check_output" "$filter" "$temp/$filter.png"
done

expect_error
expect_error "$temp/missing" identity "$temp/out.png"
expect_error tests/impulse.png unknown "$temp/out.png"
test ! -e "$temp/out.png"
expect_error tests/impulse.png identity "$temp/missing/out.png"
if [ -c /dev/full ]; then
    expect_error tests/impulse.png identity /dev/full
fi
echo 'pass CLI'
