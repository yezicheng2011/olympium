/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 */
#include <unistd.h>

int main() {
    execl("/usr/libexec/dev-desktop/olympium-run", "olympium-run", "--hold",
          "/usr/share/olympium/samples/hello32", static_cast<char *>(nullptr));
    return 127;
}
