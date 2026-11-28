' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' System tools in VB.NET (.NET 8):
'   driver-report, driver-install, install-to-disk, run-session,
'   olympium-activate, olympium-launch, olympium-mac-run, firefox-launch,
'   guest-fetch, Xwayland-software, olympium-run64, and bitness demos.

Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Text
Imports System.Threading

Namespace Olympium

    Public Module DriverTools

        Private Sub Say(msg As String)
            Console.WriteLine(msg)
            Try
                If File.Exists("/dev/kmsg") Then File.AppendAllText("/dev/kmsg", "olympium-drivers: " & msg & vbLf)
            Catch
            End Try
        End Sub

        Public Function RunReport(args As String()) As Integer
            Dim kver = UnameRelease()
            Dim arch = UnameMachine()
            Dim modDir = EnvOr("OLYMPIUM_MOD_DIR", Path.Combine("/lib/modules", kver))
            Say($"kernel {kver} arch {arch}")
            If Not Directory.Exists(modDir) Then
                Say("no module tree at " & modDir)
                Return 0
            End If

            Dim moduleFiles As New HashSet(Of String)(StringComparer.Ordinal)
            Dim moduleBuiltin As New HashSet(Of String)(StringComparer.Ordinal)
            Try
                For Each f In Directory.EnumerateFiles(modDir, "*.ko*", SearchOption.AllDirectories)
                    Dim fn = Path.GetFileName(f)
                    Dim idx = fn.IndexOf(".ko", StringComparison.Ordinal)
                    If idx > 0 Then moduleFiles.Add(fn.Substring(0, idx).Replace("-"c, "_"c))
                Next
            Catch
            End Try
            Dim builtinPath = Path.Combine(modDir, "modules.builtin")
            If File.Exists(builtinPath) Then
                For Each line In File.ReadAllLines(builtinPath)
                    Dim fn = Path.GetFileName(line.Trim())
                    If fn.EndsWith(".ko") Then fn = fn.Substring(0, fn.Length - 3)
                    If Not String.IsNullOrEmpty(fn) Then moduleBuiltin.Add(fn.Replace("-"c, "_"c))
                Next
            End If

            Dim want = {
                "e1000", "virtio_net", "virtio_blk", "virtio_pci", "nvme", "ahci",
                "xhci-hcd", "xhci-pci", "usb-storage", "hid-multitouch", "i915", "xe",
                "amdgpu", "radeon", "nouveau", "qxl", "snd", "snd-hda-intel",
                "btusb", "iwlwifi", "ath9k", "bochs", "virtio-gpu"
            }
            For Each name In want
                Dim alt = name.Replace("-"c, "_"c)
                If moduleFiles.Contains(alt) Then
                    Say($"module {name} present")
                ElseIf moduleBuiltin.Contains(alt) Then
                    Say($"module {name} built-in")
                Else
                    Say($"module {name} absent")
                End If
            Next

            Dim fwRoot = "/lib/firmware"
            Dim fwDirs = {"i915", "amdgpu", "radeon", "iwlwifi", "ath10k", "rtl_nic", "rtlwifi", "qca", "brcm", "mediatek", "cirrus"}
            For Each d In fwDirs
                Dim subDir = Path.Combine(fwRoot, d)
                Dim count = 0
                Try
                    If Directory.Exists(subDir) Then
                        count = Directory.EnumerateFiles(subDir, "*", SearchOption.AllDirectories).Count()
                    ElseIf Directory.Exists(fwRoot) Then
                        count = Directory.EnumerateFiles(fwRoot, d & "*", SearchOption.TopDirectoryOnly).Count()
                    End If
                Catch
                End Try
                If count > 0 Then
                    Say($"firmware {d} files={count}")
                Else
                    Say($"firmware {d} absent")
                End If
            Next

            Dim hasDri = Directory.Exists("/usr/lib/x86_64-linux-gnu/dri") OrElse
                         Directory.Exists("/usr/lib/aarch64-linux-gnu/dri") OrElse
                         Directory.Exists("/usr/lib/i386-linux-gnu/dri")
            Say(If(hasDri, "mesa dri present", "mesa dri absent"))
            Say("proprietary nvidia and broadcom-sta are not bundled")
            Return 0
        End Function

        Public Function RunInstall(args As String()) As Integer
            Dim mode = "detect"
            Dim target = ""
            Dim i = 0
            While i < args.Length
                Select Case args(i)
                    Case "--detect" : mode = "detect"
                    Case "--install"
                        mode = "install"
                        i += 1 : If i < args.Length Then target = args(i)
                    Case "-h", "--help"
                        Console.WriteLine("Usage: driver-install [--detect] [--install nvidia[:BRANCH]|broadcom]")
                        Return 0
                    Case Else
                        Console.Error.WriteLine("Unknown argument: " & args(i))
                        Return 2
                End Select
                i += 1
            End While

            Dim pci = If(Not String.IsNullOrEmpty(FindInPath("lspci")), RunCmd({"lspci", "-nn"}, 15).Output, "")
            Console.WriteLine("PCI devices:")
            Console.Write(If(String.IsNullOrEmpty(pci), "lspci is not installed" & vbLf, pci))
            Dim usb = If(Not String.IsNullOrEmpty(FindInPath("lsusb")), RunCmd({"lsusb"}, 15).Output, "")
            Console.WriteLine("USB devices:")
            Console.Write(If(String.IsNullOrEmpty(usb), "lsusb is not installed" & vbLf, usb))

            Dim hasNvidia = pci.Contains("10de:", StringComparison.OrdinalIgnoreCase)
            Dim hasBroadcom = pci.Contains("14e4:", StringComparison.OrdinalIgnoreCase)
            If hasNvidia Then
                Console.WriteLine("NVIDIA GPU detected (vendor 10de). Command: driver-install --install nvidia:580")
            Else
                Console.WriteLine("No NVIDIA PCI device (10de) detected. Nothing to install for NVIDIA.")
            End If
            If hasBroadcom Then
                Console.WriteLine("Broadcom PCI device detected (vendor 14e4).")
            Else
                Console.WriteLine("No Broadcom PCI device (14e4) detected.")
            End If
            If mode = "detect" Then Return 0

            Dim pkg = ""
            If target = "nvidia" OrElse target = "nvidia:580" Then
                If Not hasNvidia Then Console.Error.WriteLine("Failed: no NVIDIA device detected.") : Return 1
                pkg = "nvidia-driver-580"
            ElseIf target = "nvidia:580-open" OrElse target = "nvidia:595" OrElse target = "nvidia:470" Then
                If Not hasNvidia Then Console.Error.WriteLine("Failed: no NVIDIA device detected.") : Return 1
                pkg = "nvidia-driver-" & target.Substring(7)
            ElseIf target = "broadcom" Then
                If Not hasBroadcom Then Console.Error.WriteLine("Failed: no Broadcom device detected.") : Return 1
                pkg = "broadcom-sta-dkms"
            Else
                Console.Error.WriteLine("Failed: --install needs nvidia, nvidia:580, nvidia:580-open, nvidia:595, nvidia:470, or broadcom.")
                Return 2
            End If

            Console.WriteLine($"About to install {pkg}. Type YES to continue:")
            Dim answer = Console.ReadLine()
            If answer <> "YES" Then
                Console.WriteLine("Stopped. Nothing was installed.")
                Return 1
            End If
            Dim env As New Dictionary(Of String, String) From {{"DEBIAN_FRONTEND", "noninteractive"}}
            If RunPassthrough({"apt-get", "update"}, env) <> 0 Then Return 1
            If RunPassthrough({"apt-get", "install", "-y", pkg}, env) <> 0 Then Return 1
            Console.WriteLine($"Installed {pkg}. Reboot to switch display driver.")
            Return 0
        End Function
    End Module

    Public Module SessionRunner
        Private Function CmdlineHas(flag As String) As Boolean
            Try
                Return File.Exists("/proc/cmdline") AndAlso File.ReadAllText("/proc/cmdline").Contains(flag)
            Catch
                Return False
            End Try
        End Function

        Private Sub Kmsg(msg As String)
            Try
                If File.Exists("/dev/kmsg") Then File.AppendAllText("/dev/kmsg", "<6>dev-desktop: " & msg & vbLf)
            Catch
            End Try
        End Sub

        Public Function Run(args As String()) As Integer
            Directory.CreateDirectory("/run/dev-desktop")
            NativeBridge.chmod("/run/dev-desktop", &H1EDUI)
            Kmsg($"session start arch {UnameMachine()} {UnameRelease()}")

            ' Register binfmt_misc for Windows .exe at session start so any .exe
            ' auto-launches via Wine with zero manual configuration.
            WineAutoRunner.EnsureBinfmtRegistered()

            For Each m In {"bochs", "virtio_gpu"}
                RunCmd({"modprobe", m}, 5)
            Next
            ThreadPool.QueueUserWorkItem(Sub(state)
                                             For Each m In {"e1000", "virtio_net", "virtio_blk", "virtio_pci", "nvme"}
                                                 RunCmd({"modprobe", m}, 5)
                                             Next
                                             Try
                                                 For Each d In Directory.GetDirectories("/sys/class/net")
                                                     Dim nm = Path.GetFileName(d)
                                                     If nm <> "lo" Then RunCmd({"ip", "link", "set", nm, "up"}, 5)
                                                 Next
                                                 Dim rep = RunCmd({"/usr/bin/driver-report"}, 15)
                                                 File.WriteAllText("/tmp/driver-report.txt", rep.Output)
                                             Catch
                                             End Try
                                         End Sub)

            Dim env As New Dictionary(Of String, String) From {
                {"BROWSER", "/usr/bin/firefox"},
                {"MOZ_ENABLE_WAYLAND", "1"},
                {"OLYMPIUM_DESK_HOME", "/var/lib/olympium/desk"}
            }
            Directory.CreateDirectory("/var/lib/olympium/desk")
            If CmdlineHas("olympium.contrast=1") Then env("DEV_DESK_CONTRAST") = "1"
            If CmdlineHas("olympium.gles=1") Then
                env("WLR_RENDERER") = "gles2"
                env("LIBGL_ALWAYS_SOFTWARE") = "1"
                env("GALLIUM_DRIVER") = "llvmpipe"
            ElseIf String.IsNullOrEmpty(Environment.GetEnvironmentVariable("WLR_RENDERER")) Then
                env("WLR_RENDERER") = "pixman"
            End If

            Dim rc = RunPassthrough({"/usr/bin/dev-compositor", "-n", "-s", "/usr/libexec/dev-desktop/startup.sh"}, env)
            Kmsg($"compositor exit {rc}")
            If rc <> 0 AndAlso env.GetValueOrDefault("WLR_RENDERER", "pixman") <> "pixman" Then
                env("WLR_RENDERER") = "pixman"
                env("DEV_DESK_CONTRAST") = "1"
                rc = RunPassthrough({"/usr/bin/dev-compositor", "-n", "-s", "/usr/libexec/dev-desktop/startup.sh"}, env)
            End If
            Return rc
        End Function
    End Module

    Public Module DiskInstaller
        Private Function PartPath(disk As String, n As Integer) As String
            If Not String.IsNullOrEmpty(disk) AndAlso Char.IsDigit(disk(disk.Length - 1)) Then
                Return $"{disk}p{n}"
            End If
            Return $"{disk}{n}"
        End Function

        Public Function Run(args As String()) As Integer
            Dim yes = False
            Dim idx = 0
            If idx < args.Length AndAlso args(idx) = "--yes" Then
                yes = True : idx += 1
            End If
            Dim disk = If(idx < args.Length, args(idx), EnvOr("DEV_INSTALL_DISK"))
            If NativeBridge.geteuid() <> 0 Then
                Console.Error.WriteLine("install-to-disk: run this as root.")
                Return 1
            End If
            If String.IsNullOrEmpty(disk) OrElse Not File.Exists(disk) Then
                Console.Error.WriteLine("Usage: install-to-disk [--yes] /dev/sdX")
                Return 1
            End If
            If RunCmd({"lsblk", "-dno", "TYPE", disk}, 10).Output.Trim() <> "disk" Then
                Console.Error.WriteLine($"install-to-disk: {disk} is not a whole disk.")
                Return 1
            End If
            If Not String.IsNullOrWhiteSpace(RunCmd({"lsblk", "-no", "MOUNTPOINT", disk}, 10).Output) Then
                Console.Error.WriteLine($"install-to-disk: {disk} has a mounted file system.")
                Return 1
            End If

            Dim efi = Directory.Exists("/sys/firmware/efi")
            Dim target = "/mnt/olympium-target"
            Dim label = "olympium-root"
            If Not yes Then
                Console.Write($"Everything on {disk} will be erased. Type {disk} to continue: ")
                If Console.ReadLine() <> disk Then
                    Console.Error.WriteLine("install-to-disk: not confirmed. Nothing was changed.")
                    Return 1
                End If
            End If

            Try
                Console.WriteLine($"install-to-disk: partitioning {disk} ({If(efi, "UEFI", "BIOS")})")
                If RunCmd({"wipefs", "-a", disk}, 30).ExitCode <> 0 Then Return 1
                Dim table = If(efi, "label: gpt" & vbLf & ",512MiB,U" & vbLf & ",,L" & vbLf, "label: dos" & vbLf & ",,L,*" & vbLf)
                If RunCmd({"sh", "-c", $"printf '%s' {ShQuote(table)} | sfdisk --quiet {ShQuote(disk)}"}, 60).ExitCode <> 0 Then Return 1
                RunCmd({"partprobe", disk}, 30)
                RunCmd({"udevadm", "settle"}, 30)

                Dim esp = If(efi, PartPath(disk, 1), "")
                Dim rootdev = If(efi, PartPath(disk, 2), PartPath(disk, 1))
                If RunCmd({"mkfs.ext4", "-q", "-F", "-L", label, rootdev}, 300).ExitCode <> 0 Then Return 1
                Directory.CreateDirectory(target)
                If RunCmd({"mount", rootdev, target}, 30).ExitCode <> 0 Then Return 1
                If efi Then
                    If RunCmd({"mkfs.vfat", "-F", "32", "-n", "OLYMPIUMEFI", esp}, 60).ExitCode <> 0 Then Return 1
                    Directory.CreateDirectory(Path.Combine(target, "boot", "efi"))
                    If RunCmd({"mount", esp, Path.Combine(target, "boot", "efi")}, 30).ExitCode <> 0 Then Return 1
                End If

                Console.WriteLine("install-to-disk: copying the system")
                Dim copyCmd = $"tar -C / -cf - --one-file-system --exclude=./proc --exclude=./sys --exclude=./dev --exclude=./run --exclude=./tmp --exclude=./mnt --exclude=./media --exclude=./var/lib/olympium --exclude=./etc/fstab . | tar -C {ShQuote(target)} -xf -"
                If RunPassthrough({"sh", "-c", copyCmd}) <> 0 Then Return 1
                For Each d In {"proc", "sys", "dev", "run", "mnt", "media", "tmp"}
                    Directory.CreateDirectory(Path.Combine(target, d))
                Next
                NativeBridge.chmod(Path.Combine(target, "tmp"), &H3FFUI)

                Dim fstab = $"LABEL={label} / ext4 errors=remount-ro 0 1{vbLf}" &
                            If(efi, "LABEL=OLYMPIUMEFI /boot/efi vfat umask=0077 0 1" & vbLf, "")
                File.WriteAllText(Path.Combine(target, "etc", "fstab"), fstab)
                File.WriteAllText(Path.Combine(target, "etc", "machine-id"), "")

                Dim boot = Path.Combine(target, "boot")
                Dim kout = RunCmd({"sh", "-c", "cd " & ShQuote(boot) & " && ls vmlinuz-* 2>/dev/null | sort -V | tail -1 || true"}, 15).Output.Trim()
                If String.IsNullOrEmpty(kout) Then
                    Console.Error.WriteLine($"install-to-disk: no kernel was copied to {boot}.")
                    Return 1
                End If
                Dim version = kout.Substring("vmlinuz-".Length)
                If Not File.Exists(Path.Combine(boot, "initrd.img-" & version)) Then
                    Console.Error.WriteLine($"install-to-disk: no initrd for kernel {version} was copied.")
                    Return 1
                End If
                Directory.CreateDirectory(Path.Combine(boot, "grub"))
                Dim grubCfg =
                    "set timeout=3" & vbLf &
                    "set default=0" & vbLf &
                    "insmod part_msdos" & vbLf &
                    "insmod part_gpt" & vbLf &
                    "insmod ext2" & vbLf &
                    "search --no-floppy --label " & label & " --set=root" & vbLf &
                    "menuentry ""Olympium"" {" & vbLf &
                    vbTab & "linux /boot/" & kout & " root=LABEL=" & label & " ro quiet" & vbLf &
                    vbTab & "initrd /boot/initrd.img-" & version & vbLf &
                    "}" & vbLf &
                    "menuentry ""Olympium, contrast"" {" & vbLf &
                    vbTab & "linux /boot/" & kout & " root=LABEL=" & label & " ro quiet olympium.contrast=1" & vbLf &
                    vbTab & "initrd /boot/initrd.img-" & version & vbLf &
                    "}" & vbLf
                File.WriteAllText(Path.Combine(boot, "grub", "grub.cfg"), grubCfg)

                Dim machine = UnameMachine()
                Dim grubTarget = "x86_64-efi"
                If machine = "aarch64" OrElse machine = "arm64" Then
                    grubTarget = "arm64-efi"
                ElseIf machine = "i386" OrElse machine = "i686" Then
                    grubTarget = "i386-efi"
                ElseIf efi AndAlso machine <> "x86_64" AndAlso machine <> "x64" Then
                    Console.Error.WriteLine($"install-to-disk: no boot loader target for {machine}.")
                    Return 1
                End If
                Dim grc = If(efi,
                    RunPassthrough({"grub-install", $"--target={grubTarget}", $"--efi-directory={target}/boot/efi", $"--boot-directory={target}/boot", "--removable", "--no-nvram"}),
                    RunPassthrough({"grub-install", "--target=i386-pc", $"--boot-directory={target}/boot", disk}))
                If grc <> 0 Then Return 1
                NativeBridge.sync()
                Console.WriteLine($"install-to-disk: done. Olympium is on {disk}.")
                Return 0
            Finally
                RunCmd({"umount", Path.Combine(target, "boot", "efi")}, 15)
                RunCmd({"umount", target}, 15)
            End Try
        End Function
    End Module

    Public Module SystemTools

        Public Function RunActivate(args As String()) As Integer
            If args.Length = 0 OrElse String.IsNullOrEmpty(args(0)) Then
                Console.Error.WriteLine("Usage: olympium-activate LICENSE-KEY")
                Console.Error.WriteLine("No license was written.")
                Return 2
            End If
            Try
                Directory.CreateDirectory("/var/lib/olympium")
                File.WriteAllText("/var/lib/olympium/license", args(0) & vbLf)
                Console.WriteLine("License recorded. The purchase reminder will stay hidden.")
                Console.WriteLine("Restart the desktop session if the reminder is already on screen.")
                Return 0
            Catch ex As Exception
                Console.Error.WriteLine("olympium-activate: " & ex.Message)
                Return 1
            End Try
        End Function

        Public Function RunOlympiumLaunch(args As String()) As Integer
            Dim env As New Dictionary(Of String, String)()
            If String.IsNullOrEmpty(Environment.GetEnvironmentVariable("GDK_BACKEND")) Then env("GDK_BACKEND") = "wayland"
            If String.IsNullOrEmpty(Environment.GetEnvironmentVariable("GSK_RENDERER")) Then env("GSK_RENDERER") = "cairo"
            For Each cand In {"/usr/bin/olympium-linux", "/usr/bin/linkspan-linux", "/usr/local/bin/olympium-linux"}
                If IsExecutable(cand) Then
                    Dim argv As New List(Of String) From {cand}
                    argv.AddRange(args)
                    Return RunPassthrough(argv, env)
                End If
            Next
            Return RunPassthrough({"/usr/bin/dev-settings", "--page=olympium"}, env)
        End Function

        ' Launches Intel (x86_64/i386), Universal2 (Intel+M-series), and Apple Silicon
        ' (M-series arm64) macOS apps & Mach-O binaries through Darling GUI/CLI runtime.
        Public Function RunMacRunner(args As String()) As Integer
            Dim env As New Dictionary(Of String, String)()
            If EnvOr("OLYMPIUM_MAC_SOFTWARE") = "1" Then
                env("LIBGL_ALWAYS_SOFTWARE") = "1"
                env("GALLIUM_DRIVER") = "softpipe"
                env("LP_NUM_THREADS") = "0"
                env("GALLIUM_THREAD") = "0"
                env("mesa_glthread") = "false"
                env("MESA_SHADER_CACHE_DISABLE") = "true"
            End If
            Dim xdisplay = FindXDisplay()
            If Not String.IsNullOrEmpty(xdisplay) Then
                env("DISPLAY") = xdisplay
            End If

            Dim darling = FindInPath("darling")
            If String.IsNullOrEmpty(darling) AndAlso IsExecutable("/usr/local/bin/darling") Then
                darling = "/usr/local/bin/darling"
            End If
            Dim darlingArm64 = If(IsExecutable("/usr/local/bin/darling-arm64"), "/usr/local/bin/darling-arm64",
                               If(IsExecutable("/opt/olympium/darling-arm64/bin/darling"), "/opt/olympium/darling-arm64/bin/darling", ""))

            If args.Length >= 1 AndAlso args(0) = "--check" Then
                Return If(Not String.IsNullOrEmpty(darling) OrElse Not String.IsNullOrEmpty(darlingArm64), 0, 1)
            End If
            If args.Length < 1 Then
                Console.Error.WriteLine("usage: olympium-mac-run [APP] PROGRAM [ARG...]")
                Return 2
            End If

            ' Support both `olympium-mac-run APP PROGRAM [ARG...]` and direct binfmt `olympium-mac-run PROGRAM [ARG...]`.
            Dim program As String
            Dim extraStart As Integer
            If args.Length >= 2 AndAlso (args(0).EndsWith(".app", StringComparison.OrdinalIgnoreCase) OrElse Directory.Exists(args(0)) OrElse args(0) = args(1)) Then
                program = args(1)
                extraStart = 2
            Else
                program = args(0)
                extraStart = 1
            End If

            If Not program.StartsWith("/") Then
                program = RealPath(program)
            End If
            If Not File.Exists(program) Then
                Console.Error.WriteLine($"olympium-mac-run: program not found: {program}")
                Return 2
            End If

            ' Ensure live-ISO overlayfs DPREFIX sits on tmpfs so Darling's container mounts cleanly.
            If String.IsNullOrEmpty(Environment.GetEnvironmentVariable("DPREFIX")) Then
                Dim uid = NativeBridge.getuid()
                Dim xdg = Environment.GetEnvironmentVariable("XDG_RUNTIME_DIR")
                Dim tmpBase = If(Not String.IsNullOrEmpty(xdg) AndAlso Directory.Exists(xdg), xdg, "/run")
                Dim macDir = Path.Combine(tmpBase, $"olympium-mac-{uid}")
                Try
                    Directory.CreateDirectory(macDir)
                    NativeBridge.chmod(macDir, &H1C0UI)
                    env("DPREFIX") = Path.Combine(macDir, "prefix")
                Catch
                End Try
            End If

            ' Probe Mach-O architectures (Intel x86_64/i386 vs Apple Silicon M-series arm64 vs Universal2).
            Dim hasX64 = False
            Dim hasArm64 = False
            Try
                Dim probeBuf(4095) As Byte
                Using fs = File.OpenRead(program)
                    Dim n = fs.Read(probeBuf, 0, probeBuf.Length)
                    Dim desc As New StringBuilder(200)
                    If NativeBridge.olympium_macho_probe(probeBuf, New UIntPtr(CUInt(n)), desc, New UIntPtr(200UI)) = 0 Then
                        Dim d = desc.ToString().ToLowerInvariant()
                        hasX64 = d.Contains("x86_64") OrElse d.Contains("i386") OrElse d.Contains("fat")
                        hasArm64 = d.Contains("arm64") OrElse d.Contains("fat")
                    End If
                End Using
            Catch
            End Try

            Dim host = UnameMachine()
            Dim argv As New List(Of String)()

            ' Route pure M-series (arm64-only) binaries to ARM64 Darling (via qemu-aarch64-static on x86_64).
            If hasArm64 AndAlso Not hasX64 Then
                If Not String.IsNullOrEmpty(darlingArm64) Then
                    If (host = "x86_64" OrElse host = "x64") AndAlso IsExecutable("/usr/bin/qemu-aarch64-static") Then
                        argv.Add("/usr/bin/qemu-aarch64-static")
                    End If
                    argv.Add(darlingArm64)
                ElseIf Not String.IsNullOrEmpty(darling) AndAlso (host = "aarch64" OrElse host = "arm64") Then
                    argv.Add(darling)
                Else
                    Console.Error.WriteLine("olympium-mac-run: this is an ARM64-only (M-series) Mach-O binary; stage /opt/olympium/darling-arm64 or use a Universal2/Intel build with Darling.")
                    Return 3
                End If
            Else
                ' Intel x86_64/i386 or Universal2 (Intel + M-series): run through Darling GUI runtime.
                If String.IsNullOrEmpty(darling) Then
                    Console.Error.WriteLine("olympium-mac-run: Darling is not installed")
                    Return 3
                End If
                If (host = "aarch64" OrElse host = "arm64") Then
                    Dim box64 = FindInPath("box64")
                    If Not String.IsNullOrEmpty(box64) Then
                        argv.Add(box64)
                    ElseIf IsExecutable("/usr/bin/qemu-x86_64-static") Then
                        argv.Add("/usr/bin/qemu-x86_64-static")
                    End If
                End If
                argv.Add(darling)
            End If

            ' Warm up Darling container before executing the Mac app.
            Dim warmBin = argv(argv.Count - 1)
            For tries As Integer = 0 To 8
                Dim wargs As New List(Of String)(argv) From {"shell", "true"}
                If RunCmd(wargs, 10, env).ExitCode = 0 Then Exit For
                Thread.Sleep(500)
            Next

            argv.Add(program)
            For i As Integer = extraStart To args.Length - 1
                argv.Add(args(i))
            Next
            Return RunPassthrough(argv, env)
        End Function

        Public Function RunFirefoxLaunch(args As String()) As Integer
            Dim realBin = {"/usr/lib/firefox/firefox", "/usr/lib/firefox-esr/firefox-esr"}.FirstOrDefault(Function(c) IsExecutable(c))
            If String.IsNullOrEmpty(realBin) Then
                Console.Error.WriteLine("firefox: no Firefox program is installed")
                Return 1
            End If
            Dim wl = EnvOr("WAYLAND_DISPLAY", "wayland-0")
            Dim xdg = EnvOr("XDG_RUNTIME_DIR", "/run/dev-desktop")
            Dim sock = Path.Combine(xdg, wl)
            If Not File.Exists(sock) Then sock = Path.Combine("/run/dev-desktop", wl)
            Dim uidOut = RunCmd({"id", "-u", "olympium"}, 5)
            If uidOut.ExitCode <> 0 Then
                Dim direct As New List(Of String) From {realBin}
                direct.AddRange(args)
                Return RunPassthrough(direct)
            End If
            Dim uid = uidOut.Output.Trim()
            Dim gidOut = RunCmd({"id", "-g", "olympium"}, 5)
            Dim runtime = "/run/user/" & uid
            Directory.CreateDirectory(runtime)
            NativeBridge.chmod(runtime, &H1C0UI)
            Dim uidNum As UInteger
            Dim gidNum As UInteger
            If UInteger.TryParse(uid, uidNum) AndAlso gidOut.ExitCode = 0 AndAlso UInteger.TryParse(gidOut.Output.Trim(), gidNum) Then
                NativeBridge.chown(runtime, uidNum, gidNum)
            End If
            If File.Exists(sock) Then
                Try
                    NativeBridge.chmod(sock, &H1B6UI) ' 0666
                    Dim link = Path.Combine(runtime, wl)
                    If File.Exists(link) Then File.Delete(link)
                    File.CreateSymbolicLink(link, sock)
                Catch
                End Try
            End If
            Dim buildArgs = Function(disableSandbox As Boolean) As List(Of String)
                                Dim l As New List(Of String) From {
                                    "runuser", "-u", "olympium", "--", "env",
                                    "HOME=/home/olympium", "USER=olympium", "LOGNAME=olympium",
                                    "XDG_RUNTIME_DIR=" & runtime, "WAYLAND_DISPLAY=" & wl,
                                    "GDK_BACKEND=wayland", "MOZ_ENABLE_WAYLAND=1",
                                    "MOZ_WEBRENDER=0", "MOZ_ACCELERATED=0",
                                    "LIBGL_ALWAYS_SOFTWARE=1", "GALLIUM_DRIVER=llvmpipe", "MOZ_DBUS_REMOTE=1"
                                }
                                If disableSandbox Then l.Add("MOZ_DISABLE_CONTENT_SANDBOX=1")
                                l.AddRange({"/usr/bin/dbus-run-session", "--", realBin})
                                l.AddRange(args)
                                Return l
                            End Function
            Dim rc = RunPassthrough(buildArgs(False))
            If rc <> 0 Then rc = RunPassthrough(buildArgs(True))
            Return rc
        End Function

        Public Function RunGuestFetch(args As String()) As Integer
            Dim pin = If(args.Length >= 1 AndAlso Not String.IsNullOrEmpty(args(0)), args(0), "/usr/share/olympium/guest-pins.txt")
            Dim dest = If(args.Length >= 2 AndAlso Not String.IsNullOrEmpty(args(1)), args(1), "/var/lib/olympium/guests")
            Directory.CreateDirectory(dest)
            If Not File.Exists(pin) Then
                Console.Error.WriteLine("no pin file: " & pin)
                Return 1
            End If
            For Each raw In File.ReadAllLines(pin)
                Dim line = raw.Trim()
                If String.IsNullOrEmpty(line) OrElse line.StartsWith("#") Then Continue For
                Dim cols = line.Split({" "c, vbTab(0)}, StringSplitOptions.RemoveEmptyEntries)
                If cols.Length < 4 Then Continue For
                Dim name = cols(0) : Dim url = cols(1) : Dim sha = cols(2) : Dim policy = cols(3)
                If policy <> "first-use" Then
                    Console.WriteLine($"refuse {name}: policy '{policy}' does not allow a download")
                    Continue For
                End If
                If name.Contains("/"c) OrElse name = "." OrElse name = ".." OrElse name.StartsWith("-") Then Return 1
                If Not url.StartsWith("https://") Then Return 1
                Dim outPath = Path.Combine(dest, name)
                Dim part = outPath & ".partial"
                Console.WriteLine("fetch " & name)
                If RunPassthrough({"curl", "-fL", "--retry", "2", "-o", part, "--", url}) <> 0 Then
                    If File.Exists(part) Then File.Delete(part)
                    Return 1
                End If
                If FileDigest(part, "sha256") <> sha Then
                    If File.Exists(part) Then File.Delete(part)
                    Console.Error.WriteLine("checksum mismatch for " & name)
                    Return 1
                End If
                File.Move(part, outPath, True)
                Console.WriteLine("kept " & outPath)
            Next
            Return 0
        End Function

        Public Function RunXwaylandSoftware(args As String()) As Integer
            Dim argv As New List(Of String) From {"/usr/bin/Xwayland", "-extension", "MIT-SHM"}
            argv.AddRange(args)
            Return RunPassthrough(argv)
        End Function

        Public Function RunBitnessHelper(which As String) As Integer
            Dim runner = "/usr/libexec/dev-desktop/olympium-run"
            Select Case which
                Case "olympium-hello32" : Return RunPassthrough({runner, "--hold", "/usr/share/olympium/samples/hello32"})
                Case "olympium-hello64" : Return RunPassthrough({runner, "--hold", "/usr/lib/olympium/amd64/bin/hello64"})
                Case "olympium-bash64" : Return RunPassthrough({runner, "--hold", "/usr/lib/olympium/amd64/bin/bash"})
                Case "olympium-64demo"
                    Console.WriteLine("64-bit programs on this 32-bit system")
                    For Each p In {"/usr/lib/olympium/amd64/bin/hello64", "/usr/lib/olympium/amd64/bin/highmap64", "/usr/lib/olympium/amd64/bin/bash", "/usr/lib/olympium/amd64/bin/payload64"}
                        Console.WriteLine(vbLf & "== " & Path.GetFileName(p) & " ==")
                        RunPassthrough({runner, p})
                    Next
                    Console.WriteLine(vbLf & "done")
                    Return 0
                Case Else
                    Return 2
            End Select
        End Function

    End Module

End Namespace
