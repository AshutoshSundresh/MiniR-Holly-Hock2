#!/bin/sh
# Runs every tests/*.R script through minir --echo and compares the transcript
# with tests/expected/<name>.out.
#
#   sh tests/run_tests.sh            # uses ./minir or ./minir.exe
#   sh tests/run_tests.sh path/to/minir
#   sh tests/run_tests.sh --update   # regenerate the expected transcripts

cd "$(dirname "$0")/.." || exit 2

update=0
bin=""
for arg in "$@"; do
    if [ "$arg" = "--update" ]; then update=1; else bin="$arg"; fi
done
if [ -z "$bin" ]; then
    for candidate in ./minir ./minir.exe; do
        if [ -x "$candidate" ]; then bin="$candidate"; break; fi
    done
fi
if [ -z "$bin" ]; then
    echo "minir binary not found; build it first (make, or build_msvc.bat)"
    exit 2
fi

mkdir -p tests/expected
pass=0
fail=0
for script in tests/*.R; do
    name=$(basename "$script" .R)
    expected="tests/expected/$name.out"
    # Scripts may stop on purpose with an error, so only the output is compared
    actual=$("$bin" --echo "$script" 2>&1 | tr -d '\r')

    if [ "$update" -eq 1 ]; then
        printf '%s\n' "$actual" > "$expected"
        echo "updated  $name"
        continue
    fi

    if [ -f "$expected" ] && [ "$actual" = "$(tr -d '\r' < "$expected")" ]; then
        echo "PASS  $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name"
        printf '%s\n' "$actual" | diff "$expected" - | head -20
        fail=$((fail + 1))
    fi
done

[ "$update" -eq 1 ] && exit 0
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
