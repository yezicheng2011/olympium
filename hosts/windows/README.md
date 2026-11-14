# Linkspan for Windows

This is the Windows client. It follows the Windows light and dark appearance, and it can follow the system setting.

The engine is the Rust `linkspan` command from the repository root. This project does not bundle Wine, an Android image, or a Windows license.

## Build the engine

From the repository root, with Rust 1.85 or newer:

```bash
cargo build -p linkspan-cli --release
```

Put `target/release/linkspan.exe` on `PATH`, or set `LINKSPAN_CLI` to that file.

## Build this client

The desktop window needs the .NET 8 Windows Desktop workload, so build it on Windows:

```bash
dotnet test hosts/windows/Linkspan.Protocol.Tests
dotnet build hosts/windows/Linkspan.Windows -c Release
```

`Linkspan.Protocol` is the part that reads the compatibility file and refuses macOS and iOS software. Its tests run anywhere `dotnet` is installed, including Linux.

There is no installer script in this repository.
