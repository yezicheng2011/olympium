# Instruction translation is not an application ABI

This desktop is a native Linux system. On an x86_64 machine the kernel and the desktop are x86_64. On an aarch64 machine they are aarch64. Neither image requires KVM or HVF to display the desktop.

Running a macOS graphical application needs several layers. Each layer is necessary. None of them replaces the next one. iOS is not part of this system.

| Layer | What it does | What it does not do |
| --- | --- | --- |
| ISA translation | Translates machine instructions between x86_64 and aarch64 in software | Parse Mach-O, load dyld, or draw AppKit or UIKit |
| Mach-O / fat loader | Maps segments from a Mach-O or fat header | Provide Apple's libraries or a window server |
| dyld and libSystem | Loads dependent libraries and serves the system calls those libraries expect | Implement AppKit or UIKit |
| Objective-C runtime | Sends messages and builds classes | Provide widgets |
| AppKit | Draws a macOS graphical application | Run a Linux program or supply Apple's frameworks |

A pure software translator, including one that does not use KVM or HVF, only covers the first row. The Olympium repository is where that translator is being written. This image does not contain that translator and does not claim it is finished.

`dev-compat probe` reads a header and stops. A Linux ELF, including the lawful test app and any other program built in this repository, is not a Mac application.

This image does not ship Apple frameworks, a macOS system image, a code-signature bypass, or a DRM bypass. iOS is not part of this system.
