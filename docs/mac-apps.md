# Mac applications

A Mac application arrives as a disk image (`.dmg`), an installer package (`.pkg`), or a bare `.app` folder. This desktop opens all three. Opening means reading what is inside and copying the application into this desktop's own Applications folder, where Launchpad shows it with its own icon.

Opening does not mean running. Nothing in the file is run to install it. An installed application is started through Darling, the open-source project that runs macOS programs on Linux the way Wine runs Windows programs. Darling is not in this image. Without it, opening an application says so in a window, and the application stays installed.

No Mac program was run for anything on this page. The build machine could not get Darling: it is in neither Ubuntu's nor Debian's archive, and the network there does not reach where Darling is published.

## What each file is and what Install does with it

| File | How it is read | What is installed |
| --- | --- | --- |
| `.dmg` | 7-Zip opens the container and the volume inside it. HFS+ volumes were tested, alone and behind an Apple partition map, with zlib and bzip2 blocks. | Each application folder in the volume. Nothing else from the volume. |
| `.dmg` with more than one volume | 7-Zip lists the volumes as files and opens none. Each is taken out in turn and looked into. | The applications in the first volume that has any. |
| `.pkg` | 7-Zip reads the xar archive. `Distribution` gives the title, `PackageInfo` names the application bundles. The payload is a cpio archive packed with gzip or with pbzx, and is decoded here and unpacked by `cpio`. | Each application folder in the payloads. |
| `.pkg` with no application | Read the same way. | Nothing. A package like that changes a Mac system, and there is no Mac system here to change. |
| `.app` folder | Its `Info.plist` is read. | A copy of the folder. |
| A Mach-O file on its own | As before: its header is identified. | Nothing. It is kept in the library and not started. |

A package's `preinstall` and `postinstall` scripts are not run, ever. An application that depends on what its installer script would have done is installed without it.

## What Install does, in order

1. Reads the file and finds the applications in it. For a disk image it also adds up their size and stops if the disk does not have room for that and a tenth more.
2. Unpacks into a staging folder, `$OLYMPIUM_DESK_HOME/mac/.stage-PID`. Only the application folders are taken out of a disk image, not the whole volume.
3. Makes each application safe to keep. A link that points out of the application is removed. Anything that is not a file, a folder, or a link is removed. Set-user-ID and set-group-ID bits are cleared. Everything is made the property of the user who installed it.
4. Reads `Info.plist`, in either its XML or its binary form, for the name, version, program, and icon. A program name with a slash in it is not followed.
5. Moves the application to `$OLYMPIUM_DESK_HOME/mac/Applications/Name.app`. If one is already there it is set aside first and put back if the move fails, so an update never leaves half of each.
6. Writes a library row, a wrapper, and a Launchpad entry with the application's icon. Nothing is added to the dock.
7. Deletes the staging folder. A staging folder left by an install that was killed is deleted the next time anything is installed.

Up to eight applications are installed from one file.

`dev-install remove ID` takes an application away again: the folder, the library row, the wrapper, the Launchpad entry, and the icon. It only deletes a folder directly under this desktop's Applications, whatever the library row says.

## The unpacker is confined

A disk image or a package is whatever its author made it. 7-Zip and cpio run as the session's user, which on the live image is root, and both have had bugs where a crafted archive wrote outside the folder it was unpacked into. So the unpacker is not trusted with the system.

- Where the kernel has Landlock, the child that unpacks may read and execute the system's programs and libraries, read the one file it was given, and create files, folders, and links under the staging folder. It cannot open anything else, not even through a link it made itself and not even in a folder everyone can write to. It cannot create a device or a named pipe, cannot execute what it unpacked, cannot read another process's environment, and, on a kernel whose Landlock covers the network, cannot open a TCP connection. This is what the build machine did.
- Where the kernel has no Landlock and the user is root, the child gives up root first and unpacks as the user `nobody`. That is weaker. It keeps the unpacker away from the system and from everyone's files, and it leaves it able to write wherever every user can, such as `/tmp`. If that user cannot read the file, the file is refused. It is not unpacked as root.
- An ordinary user on a kernel without Landlock gets no extra confinement: the unpacker can do what that user can do.

`dev-install confinement` prints which of the three applies: `landlock`, `nobody`, or `none`.

The icon gets the same treatment. An `.icns` is the application's own file, and decoding a picture is done by image libraries that have had memory-safety bugs. The programs that show icons here are the installer, Files, and, for Launchpad, the compositor. None of them decodes it. A child process that can open no file and no connection decodes it and hands back 256 by 256 pixels, a block of bytes of a known length that is treated as colours and nothing else. The picture file the desktop keeps is then written from those pixels. An icon that will not decode leaves the application installed with the stand-in tile.

Launchpad applies the same rule to every program, not only Mac ones. A picture installed by a system package under `/usr`, and a picture this desktop drew itself, are read as they are. Any other, such as the icons Wine writes under the user's folder for Windows shortcuts, goes through the confined child first.

## Opening an installed application

`dev-install launch ID`, which is what the Launchpad entry runs:

1. Checks the program's SHA-256 against the one recorded at install. A program that changed is not started.
2. Reads the CPU types in the program. A universal program lists several. If none is this computer's, the application is not started, and the window says which it was built for.
3. Looks for a Mac runtime by running `/usr/libexec/dev-desktop/olympium-mac-run --check`. That script answers yes when `darling` is installed.
4. Hands the runtime the application folder, the program, and any arguments.

Each refusal is shown in a window with the application's icon, because a launch from Launchpad has no terminal to print to.

`olympium-mac-run` is a script so that the one place that knows how Darling is called can be changed without rebuilding anything. It calls `darling shell` with the program's path under `/Volumes/SystemRoot`, which is where Darling's documentation says the Linux file system appears. That call was not run against a real Darling.

This desktop does not load Mach-O programs itself and has no other way to start one. It does not check a code signature and does not get around one. It does not get around DRM. The pinned Olympium runtime's refusal to start macOS software is unchanged: Mac files are read by the desktop and are not sent to that runtime. Apple's own applications are licensed for Apple hardware and are not what this is for.

## What looks like a Mac

- Files shows an `.app` folder as one item, with its own icon and without the `.app`. A double-click opens Install, or starts the application if it is already installed. It does not open the folder.
- Files draws a disk for a `.dmg` and a box for a `.pkg`.
- Install shows the application's icon and name first, then what the file is, a box to tick, Install, and Open. While it works it shows how far it is, and Install becomes Stop.
- Launchpad lists the application by its own name with its own icon, next to Linux and Windows programs.

What is not like a Mac: a disk image is not mounted and does not appear as a volume. There is no drag to Applications. There is no Trash to drag an application into; removing one is a command.

## What was run

`make -C desktop test-mac` on the x86_64 build machine, Ubuntu 24.04, 7-Zip 23.01, GNU cpio 2.15, kernel with Landlock. All of it passed.

The files it opens are written by `desktop/mac-fixtures.py`, byte by byte: an HFS+ volume, the container that makes it a `.dmg`, a xar archive that is a `.pkg`. Three tools that are not ours agree they are well formed. `fsck.hfsplus`, which is Apple's `fsck_hfs`, reports the volume OK. 7-Zip verifies the checksums of all seven disk images. `dmg2img` turns a disk image back into the exact volume it was made from. The application inside is a Mach-O header with no code after it.

- From inside the Landlock confinement, a test program tried fourteen things. It could read its file, create a file, a folder and a link in its folder, and run a system program. It could not create a file in a folder open to everyone, could not do so through a link, could not create a device or a named pipe, could not open a system file for writing, could not read another process's environment or a file it was not given, could not run a program placed in its folder, and could not open a TCP connection.
- From inside the fallback, as `nobody`, nine things held: the same four permissions, no file in a folder that user cannot write to, none through a link to it, no device, no writing to a system file, and no way back to root.

- The readers of `Info.plist` and `.icns` were built with the address and undefined-behaviour sanitizers and given the fixture files and 60,000 damaged copies of them.
- Looking at a disk image installed nothing. Install without consent installed nothing.
- The same application installed from a plain disk image, one behind a partition map, one with bzip2 blocks, one with two volumes, a gzip package, a pbzx package with several chunks, and a bare `.app` folder. Each time the installed files were compared with the originals.
- The package's `postinstall` script, which would have left a file behind, did not run.
- A package with no application was not installed.
- A disk image whose volume holds a link pointing outside, a file filed under that link, and a link to `/etc/hostname` inside the application: the application was installed without those links and nothing was written outside. A package with a link pointing outside, a file to be written through it, a path of `../`, and an absolute path: refused, and nothing was written outside.
- A disk image with 300 bytes changed, and a package cut short: refused, and the copy already installed was untouched.
- The icon kept for an installed application was 256 by 256 and shared no stretch of bytes with the application's `.icns`. An application whose `.icns` was damaged installed without a picture.
- Opening with no runtime exited 3 and said Darling is not installed. With a stand-in script for the runtime, the script received the application folder, the program, and the arguments. An application with no code for this CPU was not handed over. A program changed after install was not handed over.
- A double-click on an application already in Applications started it instead of offering to install it.
- An application named 计算器, in a volume named 计算器, in a file named `计算器 安装.dmg`, in a folder named `下载 文件`, installed under its own name.
- `remove` took one application and left the others. With the library row edited to point at another folder, it deleted nothing.
- With the confinement forced to `nobody`, a disk image and a package installed the same files, owned by root afterwards. A file that user could not read was refused.
- On the compositor with the headless backend: the Install window mapped. Stop, pressed while a 24 MB disk image was being read, ended the install and left no staging folder. Install, pressed, installed it. Launchpad listed three Mac applications, each with its icon file. Started from Launchpad with no runtime, a window titled with the application's name mapped. Started from Launchpad with the stand-in in the helper's place, the stand-in received the program.

## What was not run

- No Mac program. Not with Darling, not any other way.
- No disk image or package from a real vendor. Only the fixtures.
- No APFS volume. 7-Zip 23.01 says it reads APFS, and this desktop passes whatever 7-Zip lists; no APFS image was made or opened here. Newer disk images are often APFS.
- No disk image with LZFSE or LZMA blocks, no encrypted disk image, and none with a license agreement in front of it.
- No package with a bzip2 payload; the decoder does not read one and says so.
- Nothing on a booted ISO. The image scripts now install `7zip`, `cpio`, and `librsvg2-common`, build against `liblzma` and `zlib`, and install `olympium-mac-run`, and none of those scripts was run.
- The fallback confinement on a kernel that really lacks Landlock. It was forced on a kernel that has it.
- The i386 and arm64 builds of any of this.
