# 32-bit scope

This image set does not include an i686 or armhf ISO, kernel, or userspace.

The desktop, the live boot path, and the instruction-translation work it integrates with are 64-bit: x86_64 and aarch64. A 32-bit image would be a separate bootstrap, a separate kernel, and a separate ISO. None of that is started here.

`dev-compat` may still *identify* a 32-bit Mach-O header. Identification does not execute it, and there is no 32-bit macOS or iOS userspace in this image.
