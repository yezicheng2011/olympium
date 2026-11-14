# Instruction translation is not an application ABI

This desktop is a native Linux system. On an x86_64 machine the kernel and the desktop are x86_64. On an aarch64 machine they are aarch64. Neither image requires KVM or HVF to display the desktop.

Running a macOS or iOS graphical application needs several layers. Each layer is necessary. None of them replaces the next one.

| Layer | What it does | What it does not do |
| --- | --- | --- |
| ISA translation | Translates machine instructions between x86_64 and aarch64 in software | Parse Mach-O, load dyld, or draw AppKit or UIKit |
| Mach-O / fat loader | Maps segments from a Mach-O or fat header | Provide Apple's libraries or a window server |
| dyld and libSystem | Loads dependent libraries and serves the system calls those libraries expect | Implement AppKit or UIKit |
| Objective-C runtime | Sends messages and builds classes | Provide widgets |
| AppKit | Draws a macOS graphical application | Run an iOS application |
| UIKit | Draws an iOS graphical application | Run a macOS application |

A pure software translator, including one that does not use KVM or HVF, only covers the first row. The Olympium repository is where that translator is being written. This image does not contain that translator and does not claim it is finished.

`dev-compat probe` reads a header and stops. A Linux ELF, including any program built in this repository, is not evidence that an original macOS or iOS GUI application runs.

This image does not ship Apple frameworks, a macOS or iOS system image, a code-signature bypass, or a DRM bypass. An IPA is not installed or decrypted.
