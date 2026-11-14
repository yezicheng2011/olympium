# Olympium integration

The desktop dock runs `/usr/libexec/dev-desktop/olympium-launch`.

That script executes `olympium-linux` or `linkspan-linux` when one of those binaries is installed. It does not clone or write the Olympium repository. The pin recorded here is:

`25a592cfd65a4f07449ca03e919bfa97626bb164`

At that revision the Linux client can launch and stop an OpenHarmony guest from its own window, and an Android Cuttlefish path exists on Linux. Both are work in progress. macOS and iOS guests are refused by that revision's policy. This image does not patch that policy.

If the binary is absent, the dock opens the Olympium page in Settings, which states the pin and the refusal. That is a launch of the integration point, not a claim that a guest booted.

Pure software x86/ARM translation belongs in the Olympium tree. This desktop stays native to each ISO architecture and does not require KVM for its own display. QEMU TCG used to boot an arm64 ISO is not Apple Silicon support and is not the Olympium translator.
