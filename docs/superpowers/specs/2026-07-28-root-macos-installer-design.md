# Root macOS Installer Design

## Goal

Expose one macOS installation entry point at the repository root while keeping
the existing installer implementation, security boundaries, release-package
behavior, and Windows installation path intact.

The public commands are:

```text
./install.sh install
./install.sh status
./install.sh uninstall
./install.sh uninstall --purge
```

## Selected Approach

Add a context-aware root `install.sh` and use the same file in both supported
layouts:

1. a source checkout containing `scripts/`; and
2. the self-contained `CardputerCompanion-mac-installer` release directory
   containing `installer/` and `CardputerCompanion.app`.

The shell entry point only resolves the layout, prepares a missing source-tree
application bundle, and dispatches to the existing Python installer. It does
not duplicate PIN collection, configuration validation, LaunchAgent, HAL,
AudioBridge, status, or uninstall logic.

## Source-Checkout Flow

For `install`, the root entry point checks for
`dist/CardputerCompanion.app`. If the bundle is absent, it runs
`scripts/build_companion.sh` once. It then dispatches the original arguments to
`scripts/mac_installer.py`, whose existing source-app resolution selects the
built bundle.

For `status` and `uninstall`, the entry point dispatches immediately and never
builds an application. This keeps read-only status and recovery/uninstall
available even when the build toolchain or source artifact is unavailable.

Unknown or missing operations are passed to the Python argument parser so its
existing usage and error behavior remain authoritative.

## Release-Package Flow

`scripts/package_mac_installer.sh` copies the root `install.sh` into the
packaged directory. In that layout, the entry point detects
`installer/mac_installer.py` and dispatches directly without attempting a
source build.

`scripts/mac_installer.sh` remains available as a compatibility entry point for
existing development automation, but is no longer the file copied into the
public package.

## Platform Boundaries

- The root shell entry point supports macOS only.
- Windows x64 continues to use the NSIS `.exe`.
- Windows ARM64 continues to use the portable Agent archive.
- This change does not add a Windows shell wrapper, install a Windows driver,
  modify firmware, or change the product version.

## Security and Failure Handling

- PIN values remain collected by the Python installer with masked input.
- No credential is added to arguments, logs, the LaunchAgent plist, Git, or
  release artifacts.
- The root entry point uses strict shell error handling and absolute paths
  derived from its own location.
- A failed source build stops installation before any installed state changes.
- A missing or invalid layout returns a clear non-zero error.
- Existing exact-target uninstall and `--purge` behavior remains unchanged.

## Documentation

The English and Chinese READMEs make the root command the primary macOS
source-checkout workflow. They separately explain that an extracted
`CardputerCompanion-mac-installer` directory exposes the same command.
Windows instructions remain unchanged.

## Verification

Automated tests will prove:

- the source layout dispatches all four public operations;
- source `install` builds only when the app bundle is absent;
- source `status` and both uninstall forms never build;
- the packaged layout never invokes the source build;
- all arguments are preserved;
- the packaging script copies the root entry point;
- the installer package and release checksums remain valid.

The final gate includes targeted installer tests, macOS package generation,
checksum verification, documentation link/command checks, a clean Git tree,
and confirmation that the pushed `origin/main` commit equals local `main`.

