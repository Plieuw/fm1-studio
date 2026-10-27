#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# STUDIO: tests/ui_test.c's functions in order, in one process (as ui_test runs them), against tests/studio_ui_xfail.txt:
# fails when a function not listed there fails. Usage: tests/studio_ui_check.sh CC OUT
set -e
CC="$1"; OUT="$2"
python3 - <<'PY'
s = open("tests/ui_test.c").read()
i = s.index("int main(void)\n{\n    setvbuf(stdout, NULL, _IONBF, 0);")
j = s.index("\n}\n", s.index("    return bad ? 1 : 0;", i)) + 3
open("build/host/ui_test_body.h", "w").write(s[:i] + s[j:])
PY
$CC -w -Ibuild/host -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/ui_test_each" tests/ui_test_each.c -lm
skip=$(awk '/SKIP:/ {print $1}' tests/studio_ui_xfail.txt | tr '\n' ' ')
UI_SEQ=1 UI_SKIP="$skip" "$OUT/ui_test_each" > "$OUT/ui_test_each.log" 2>&1 || true
bad=0
for f in $(awk '/^UIFN FAIL/ {print $3}' "$OUT/ui_test_each.log"); do
    if ! grep -q "^$f " tests/studio_ui_xfail.txt; then echo "UI: $f FAILED (not an expected Felucca-only failure)"; bad=1; fi
done
n_ok=$(grep -c "^UIFN ok" "$OUT/ui_test_each.log" || true)
n_x=$(grep -c "^UIFN FAIL" "$OUT/ui_test_each.log" || true)
echo "UI: $n_ok functions passed, $n_x expected Felucca-only failures (tests/studio_ui_xfail.txt), $(echo $skip | wc -w) skipped"
exit $bad
