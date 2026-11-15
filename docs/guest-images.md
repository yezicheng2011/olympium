# Guest system images

Guest images are not stored in git and are not placed in the ISO unless the exact artifact's redistribution license allows it. An unknown license means the artifact stays out.

| Artifact | Bundled | Why |
| --- | --- | --- |
| macOS recovery, installer, or IPSW | No | Apple's license does not allow this redistribution. |
| iOS firmware or App Store packages | No | iOS is not part of this system. |
| Windows installation media | No | Proprietary. A wrapper license does not cover it. |
| Google Play or GMS | No | Not redistributable here. |
| OpenHarmony factory images | No | The pinned Olympium revision downloads a checksummed image when its own terms allow and does not treat the image as part of the Apache-2.0 source. This ISO does not copy that download. |
| Android Cuttlefish images | No | Same rule. No image is vendored to avoid a license guess. |
| Ubuntu 24.04 packages used as the Linux base | Yes, inside the squashfs | Ubuntu publishes those packages for redistribution. Their copyright files stay in `/usr/share/doc`. Versions are listed in `sbom.tsv`. The desktop title is not Ubuntu. |

A first-use download inside Olympium, when that program is installed, remains that program's behavior: pinned checksums, no substitution of a forbidden file by a mirror, and no use of a download to get around a license that forbids redistribution. This ISO does not add a second downloader for those guests.
