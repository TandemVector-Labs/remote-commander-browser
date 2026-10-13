#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
status=0

printf '%s\n' '== shell syntax =='
for f in "$ROOT"/scripts/*.sh; do
  bash -n "$f" || status=1
done

printf '%s\n' '== source privacy/secret scan =='
pattern='(sk-[A-Za-z0-9_-]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|Bearer [A-Za-z0-9._-]{20,}|password[[:space:]]*[=:]|api[_-]?key[[:space:]]*[=:]|access[_-]?token[[:space:]]*[=:]|refresh[_-]?token[[:space:]]*[=:]|/home/[A-Za-z0-9._-]+/)'
if grep -RInE "$pattern" "$ROOT/src"; then
  printf 'Potential secret or personal absolute path found in src/.\n' >&2
  status=1
else
  printf 'CLEAN\n'
fi

printf '%s\n' '== forbidden runtime data =='
if find "$ROOT" -path "$ROOT/.git" -prune -o -type f \( -name 'Cookies' -o -name 'Login Data' -o -name 'tabs.state' -o -name 'Local State' \) -print | grep -q .; then
  printf 'Browser runtime/profile file found in repository tree.\n' >&2
  status=1
else
  printf 'CLEAN\n'
fi

printf '%s\n' '== Linux CEF fork stack-guard contract =='
if python3 - "$ROOT/src/main.cc" <<'PY_GUARD'
import re
import sys
from pathlib import Path
source = Path(sys.argv[1]).read_text()
pattern = r'(?m)^#if defined\(__linux__\)\s*\nNO_STACK_PROTECTOR\s*\n#endif\s*\nint RunBrowser\s*\('
if not re.search(pattern, source):
    print('Missing Linux NO_STACK_PROTECTOR on RunBrowser → CefExecuteProcess path.', file=sys.stderr)
    sys.exit(1)
print('LINUX_CEF_FORK_GUARD_PASS')
PY_GUARD
then
  :
else
  status=1
fi

printf '%s\n' '== sandbox bypass scan =='
if grep -RIn -- '--no-sandbox' "$ROOT/src"; then
  printf 'Source contains --no-sandbox; release rejected.\n' >&2
  status=1
else
  printf 'CLEAN\n'
fi

printf '%s\n' '== cppcheck =='
if command -v cppcheck >/dev/null 2>&1; then
  out=$(mktemp)
  trap 'rm -f "$out"' EXIT
  # Keep correctness-oriented checks fail-closed while suppressing three
  # cppcheck style-only diagnostics already present on exact v0.8.0 baseline.
  cppcheck --enable=warning,style,performance,portability --std=c++17 --force \
    --suppress=missingIncludeSystem --suppress=unmatchedSuppression \
    --suppress=functionStatic --suppress=constParameterCallback --suppress=constVariablePointer \
    --inline-suppr "$ROOT/src" 2>"$out" || true
  if [[ -s "$out" ]]; then
    cat "$out" >&2
    status=1
  else
    printf 'CLEAN\n'
  fi
else
  printf 'SKIP: cppcheck not installed\n'
fi

if [[ "$status" -ne 0 ]]; then
  printf 'STATIC_QA=FAIL\n' >&2
  exit "$status"
fi
printf 'STATIC_QA=PASS\n'
