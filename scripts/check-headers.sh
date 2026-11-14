#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0

set -eu
cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
fail=0
while IFS= read -r file
do
    if ! grep -q 'SPDX-License-Identifier: Apache-2.0' "$file"; then
        echo "missing Apache-2.0 header: $file"
        fail=1
    fi
    if grep -E 'SPDX-License-Identifier: (MIT|GPL|BSD|ISC|MPL|LGPL)' "$file" >/dev/null; then
        echo "wrong SPDX license: $file"
        fail=1
    fi
done <<EOF
$(find crates examples hosts scripts \
    \( -name obj -o -name bin -o -name target -o -name out -o -name .build -o -name .gradle \) -prune -o \
    -type f \( \
        -name '*.rs' -o -name '*.cs' -o -name '*.java' -o -name '*.kt' -o -name '*.kts' \
        -o -name '*.swift' -o -name '*.sh' -o -name '*.c' -o -name '*.js' \
        -o -name '*.ets' -o -name '*.ts' \
    \) -print)
EOF
if ! grep -q 'Apache License' LICENSE || ! grep -q 'Version 2.0, January 2004' LICENSE; then
    echo "LICENSE is not Apache-2.0"
    fail=1
fi
if ! grep -q 'Apache-2.0 only' NOTICE; then
    echo "NOTICE does not keep Apache-2.0 only"
    fail=1
fi
exit "$fail"
