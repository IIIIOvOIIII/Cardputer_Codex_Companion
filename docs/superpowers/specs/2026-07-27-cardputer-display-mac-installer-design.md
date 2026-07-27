# Cardputer Display and Mac Installer Design

## Goal

Release Cardputer Codex Companion `1.1.2` with tear-free pet rendering,
truthful Mac Agent status, a real-pet static frame while the microphone is
active, and a self-contained macOS installer that supports reliable login
startup and reversible uninstall.

## Confirmed Problems

### Pet horizontal misalignment

Commit `647fe6c` changed the pet renderer from one LCD transaction per frame to
one transaction per row. A 96x104 frame is consequently transferred as 104
independent address-window writes. When the pet moves horizontally, panel scan
and the repeated transactions expose rows from different animation frames as a
visible horizontal tear.

The fix must retain row-by-row decoding because restoring the old full RGB565
frame buffer would consume about 20 KiB of persistent RAM.

### Blue placeholder while the microphone is active

The UI page renderer clears the PET page and draws the blue placeholder whenever
the microphone changes the UI revision. Animation is then intentionally
disabled during `STARTING`, `LIVE24`, `LIVE16`, and `STOPPING`, so no real pet
frame replaces the placeholder.

### `B+W+M-`

The PET header means:

- `B`: BLE HID link;
- `W`: Wi-Fi;
- `M`: Mac Companion LAN heartbeat.

The microphone has its own `MIC` indicator. The current LaunchAgent process is
running, but its requests receive HTTP 401 because the replacement Cardputer's
PIN differs from the pairing value stored on the Mac. The `M-` indication is
therefore correct and must not be cosmetically forced to `M+`.

### Fragile Mac login startup

The current LaunchAgent points directly into a Git worktree's `dist` directory
and uses that worktree as its working directory. Removing, moving, or rebuilding
the worktree can invalidate the login item. The existing driver and LaunchAgent
helpers also require separate manual commands and do not provide one reversible
install flow.

## Design

### Firmware version

The release version is `1.1.2`. The following surfaces must agree:

- ESP-IDF `PROJECT_VER`;
- firmware `kProductVersion`;
- Companion `--version`;
- Companion application plist;
- Core Audio HAL plist;
- Codex app-server client metadata;
- release tests and checksum manifest.

A release consistency test will fail if any executable or plist surface differs
from the expected version. Historical validation documents remain unchanged.

### Atomic streamed pet frame

`display_render_pet_frame` will:

1. enter one `M5.Display.startWrite()` transaction;
2. set RGB565 byte order;
3. set one 96x104 address window at the pet coordinates;
4. ask `PetStore` to decode rows in increasing order;
5. write each row into the already-open window without changing the window;
6. restore byte order and end the transaction once.

The row callback rejects an unexpected row number. Cleanup restores display
state even when decoding fails. No full-frame buffer, dynamic allocation, or
per-row LCD transaction is introduced.

### Static real-pet frame during microphone capture

The UI controller will distinguish two rendering modes:

- `animated`: microphone is not starting, live, or stopping; render on the
  configured 2-3 FPS schedule and advance the frame index;
- `static`: microphone is starting, live, or stopping; when PET chrome or
  microphone state changes, render the selected pet's current frame exactly
  once and do not advance its index.

The PET page renderer will no longer unconditionally draw the blue placeholder
before the controller has tried the selected pet. The placeholder remains the
fallback only when no valid pet frame can be decoded. When capture stops,
animation resumes from the frozen frame.

### Truthful Mac status

The `B/W/M` header labels remain unchanged. Installation will collect the
current Cardputer URL and PIN, write the config with mode `0600`, and restart
the LaunchAgent. `M+` is reached only after an authenticated Companion request
refreshes the device heartbeat.

The PIN must never appear in:

- LaunchAgent program arguments;
- the LaunchAgent plist;
- process listings;
- installer logs;
- shell history.

Interactive installation uses hidden PIN input. Non-interactive installation
accepts an existing mode-`0600` JSON config file rather than a PIN argument.

### Mac installation layout

The distributable directory is:

```text
CardputerCompanion-mac-installer/
  install.sh
  CardputerCompanion.app/
```

The stable installed paths are:

```text
~/Applications/CardputerCompanion.app
~/Library/Application Support/CardputerCodexCompanion/config.json
~/Library/LaunchAgents/com.lynx.cardputer-companion.plist
~/Library/Logs/CardputerCodexCompanion/
/Library/Audio/Plug-Ins/HAL/CardputerCodexMicrophone.driver
/Library/PrivilegedHelperTools/com.lynx.cardputer-audio-bridge
/Library/LaunchDaemons/com.lynx.cardputer-audio-bridge.plist
```

The installer has four public operations:

```text
install.sh install
install.sh status
install.sh uninstall
install.sh uninstall --purge
```

`install` performs these operations in order:

1. validate macOS, application signature, bundled HAL, bridge, and helper;
2. collect or import device configuration without exposing the PIN;
3. atomically copy the app to `~/Applications`;
4. atomically write the protected config;
5. invoke the existing root-only audio installer with `sudo`;
6. write a LaunchAgent that points only to stable installed paths;
7. bootstrap and kick-start the LaunchAgent;
8. report LaunchAgent, audio bridge, HAL, Core Audio device, and authenticated
   LAN status independently.

`uninstall` boots out the LaunchAgent, removes its plist and installed app, and
uses the exact-target audio uninstaller. It preserves config and logs.
`uninstall --purge` additionally removes the application-support directory and
logs. Uninstall never targets unrelated applications, LaunchAgents, HAL
drivers, helpers, or LaunchDaemons.

The existing low-level audio installer remains the single owner of privileged
HAL and bridge mutation. The new top-level installer orchestrates it rather
than duplicating privileged logic.

### Auto-start behavior

The generated LaunchAgent uses:

- `RunAtLoad=true`;
- `KeepAlive=true`;
- the stable installed executable;
- the protected config file;
- a fixed executable search path that includes Homebrew and system paths;
- stable log paths;
- no dependency on the source repository or worktree.

Installation verifies `launchctl print gui/$UID/com.lynx.cardputer-companion`
and requires a running PID. A failed bootstrap, crash loop, missing config, or
authentication failure is reported distinctly.

## Error Handling and Rollback

- Application installation uses staging plus rename; a failed copy leaves the
  prior installed app available.
- Config validation happens before replacing the existing config.
- The audio installer retains its current staged backup and rollback behavior.
- A failed LaunchAgent bootstrap leaves diagnostic files and reports failure;
  it does not claim successful installation.
- `status` is read-only and safe when partially installed.
- Uninstall ignores already-absent exact targets so it is idempotent.

## Verification

Automated RED/GREEN coverage will prove:

- exactly one LCD transaction and one full-frame address window are used;
- no full RGB565 frame buffer is restored;
- microphone-live policy requests one static selected-pet frame after PET chrome
  changes and does not advance the animation index;
- every current version surface is `1.1.2`;
- installer config never places a PIN in plist or argv;
- sandbox installation uses stable paths and mode `0600`;
- `uninstall` preserves config/logs;
- `uninstall --purge` removes config/logs;
- unrelated files survive both uninstall modes;
- LaunchAgent contains `RunAtLoad`, `KeepAlive`, and no worktree path.

Release verification will include the full project gate, checksum verification,
application signature checks, app-only flash at `0x20000`, independent flash
verification, serial boot monitoring, device status, current-Mac installation,
LaunchAgent restart, authenticated `M+`, Core Audio enumeration, microphone
input, and a physical observation of the moving and frozen pet frames.

## Boundaries

- No `.pkg`, notarization, System Extension migration, or Apple Developer ID
  distribution is added in this release.
- No full-frame or double-frame display buffer is added.
- No fake Agent status or relaxation of PIN authentication is permitted.
- The installer supports the current signed local development distribution and
  macOS 14 or later.
- Full-image flashing remains recovery-only. Configured devices are updated
  app-only at `0x20000`.
