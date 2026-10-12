#!/bin/sh
set -eu

program=$1
check_output=$2
temp=$(mktemp -d "$(dirname "$program")/pipeline-cli-XXXXXX")
trap 'rm -rf "$temp"' EXIT HUP INT TERM
"$program" --help > "$temp/help"
grep -q Usage: "$temp/help"
for filter in identity blur gaussian sharpen edge; do
    "$program" "$filter" "$temp/$filter" 4 2 tests/impulse.png tests/impulse.png tests/impulse.png >"$temp/stdout" 2>"$temp/stderr"
    for n in 000000 000001 000002; do
        "$check_output" "$filter" "$temp/$filter/$n.png"
    done
    grep -q 'read=3 processed=3 written=3' "$temp/stderr"
done
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
expect_error
expect_error identity "$temp/invalid-options" 0 2 tests/impulse.png
expect_error identity "$temp/invalid-options" 2 0 tests/impulse.png
expect_error identity "$temp/invalid-options" -1 2 tests/impulse.png
expect_error identity "$temp/invalid-options" 2x 2 tests/impulse.png
expect_error identity "$temp/invalid-options" 2 99999999999999999999999999 tests/impulse.png
expect_error unknown "$temp/invalid-options" 2 2 tests/impulse.png
test ! -e "$temp/invalid-options"

mkdir "$temp/existing-empty"
expect_error identity "$temp/existing-empty" 2 2 tests/impulse.png
test ! -e "$temp/existing-empty/000000.png"
cp tests/impulse.png "$temp/existing-empty/000000.png"
expect_error edge "$temp/existing-empty" 2 2 tests/impulse.png
cmp tests/impulse.png "$temp/existing-empty/000000.png"
cp tests/impulse.png "$temp/existing-file"
expect_error identity "$temp/existing-file" 2 2 tests/impulse.png
cmp tests/impulse.png "$temp/existing-file"
expect_error identity "$temp/missing-parent/out" 2 2 tests/impulse.png
test ! -e "$temp/missing-parent"

expect_error identity "$temp/missing-input-output" 2 2 "$temp/missing.png"
grep -q 'pipeline failed: read' "$temp/stderr"
printf 'not a picture' > "$temp/broken.png"
expect_error identity "$temp/broken-input-output" 2 2 tests/impulse.png "$temp/broken.png" tests/impulse.png
grep -q 'pipeline failed: read' "$temp/stderr"
printf 'pass pipeline CLI\n'
