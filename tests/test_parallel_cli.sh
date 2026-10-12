#!/bin/sh
set -eu

program=$1
check_output=$2
temp=$(mktemp -d "$(dirname "$program")/parallel-cli-XXXXXX")
trap 'rm -rf "$temp"' EXIT HUP INT TERM

expect_error() {
    if "$program" tests/impulse.png identity "$temp/out.png" "$@" >"$temp/stdout" 2>"$temp/stderr"; then
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

for mode in pixels rows columns grid; do
    for filter in identity blur gaussian sharpen edge; do
        if [ "$mode" = grid ]; then
            "$program" tests/impulse.png "$filter" "$temp/out.png" "$mode" 3 4 2
        else
            "$program" tests/impulse.png "$filter" "$temp/out.png" "$mode" 3
        fi
        "$check_output" "$filter" "$temp/out.png"
    done
done

expect_error unknown 2
expect_error rows 0
expect_error rows -1
expect_error rows 2x
expect_error rows 99999999999999999999999999999
expect_error grid 3
expect_error grid 3 0 2
expect_error rows 3 4 2
echo 'pass parallel CLI'
