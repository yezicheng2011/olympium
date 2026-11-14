# Android and OpenHarmony guest builds

This note is for deciding where a real guest image could be built. It does not
publish an image. No placeholder archive was written. AX41 was not used.

## What was checked, and not used

Google's published AOSP workstation page, updated 2026-11-04, says a current
AOSP tree should have at least 400 GB free disk (250 GB to check out and 150 GB
to build) and a minimum of 64 GB of RAM, on 64-bit x86 Linux with glibc 2.17 or
later:

https://source.android.com/docs/setup/start/requirements

The same page says Google uses 72-core machines with 64 GB of RAM, and that a
6-core machine with 64 GB of RAM takes about 6 hours for a full build. Core
count there is a statement about duration. It is not a claim that a compile
with a lower `-j` is impossible.

The source for an emulator image is the public AOSP manifest, for example
`android-latest-release`, then a user or userdebug lunch such as
`aosp_x86_64` or `sdk_phone_x86_64`, with no Google Play packages. The build
commands are the public ones (`repo init`, `repo sync`, `source build/envsetup.sh`,
`lunch`, `m`). Output used by the pack script is `system.img` and
`userdata.img` in the product directory. `scripts` in
[olympium-android](https://github.com/yezicheng2011/olympium-android) pack only
that local output. They do not download an image.

Emulator system images from the Android SDK are a different license from AOSP
source. Those SDK downloads were not fetched and are not repackaged. A file
from that channel is not treated as a redistributable Olympium guest.

## OpenHarmony

The public QEMU board instructions are source and configuration, not a system
image Olympium can republish:

https://gitee.com/openharmony/device_qemu/blob/HEAD/arm_virt/linux/README.md

That page builds `qemu-arm-linux-min` or `qemu-arm-linux-headless` with
`./build.sh --product-name qemu-arm-linux-min --ccache --jobs 4` from an
OpenHarmony manifest, then runs `qemu_run.sh`. The `--jobs 4` in that example
is upstream's own command. It is not a 16-core requirement. The license of the
`device_qemu` repository is Apache-2.0. Third-party components in a built
system keep their own licenses. HarmonyOS commercial images, Huawei update
packages, and Huawei signing keys are out of scope.

No official prebuilt QEMU system image was found that is both publicly
redistributable and verified to boot here. A third-party image collection was
not downloaded and is not described as an OpenHarmony release.

## Capacity that was reported, and why it is not a build host

AX41 is a production Ubuntu machine: Ryzen 5 3600, 6 cores and 12 threads,
62 GiB RAM with about 60 GiB available, no swap, and a commit limit of about
31.3 GiB. Its RAID1 root is 468 GB with about 255 GB free, and it already
serves live sites. A full AOSP checkout plus build output, at the sizes Google
publishes, does not fit in that free space without displacing those sites.
The commit limit is also below the 64 GB RAM figure Google publishes. This
repository does not change swap, services, or files on that machine.

This disposable build VM has 4 vCPU, 16 GiB RAM, and about 235 GB free. That
is also below the published AOSP disk and RAM figures, so the source tree was
not synced here either.

A lower `-j` can be the right way to compile when RAM is the limit and disk
still holds the tree. It does not create the missing disk, and it does not
turn an SDK zip or an unverified third-party image into a guest.

## What remains

An Android or OpenHarmony guest that Olympium can download still requires a
build on a machine with room for the source tree and the output, followed by
a boot of that output before any archive is published. Until that boot exists,
both clients report the image as not published.
