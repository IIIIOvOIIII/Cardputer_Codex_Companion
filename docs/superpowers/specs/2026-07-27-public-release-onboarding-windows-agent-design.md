# Cardputer Codex Companion 1.2.0 Public Release Design

## Goal

Prepare Cardputer Codex Companion for a public `1.2.0` release without
publishing private Wi-Fi or pairing material, add a device-led first-run
onboarding flow, make navigation back to the pet page consistent, and deliver
a Windows Machine Agent alongside the existing macOS Agent.

The final local state must also be suitable for a clean new-device
end-to-end test: after all builds and Mac-side verification finish, the
currently installed macOS Agent, launch jobs, audio driver, helper, pairing
configuration, logs, and private firmware artifacts are removed.

## Confirmed Decisions

- The firmware, protocol, agents, and release artifacts advance to `1.2.0`.
- Onboarding is device-led. Wi-Fi selection and password input happen on the
  Cardputer, not through a temporary provisioning website.
- Onboarding completes only after Wi-Fi connectivity, BLE HID pairing, and an
  authenticated Machine Agent heartbeat have all been observed.
- On every normal non-pet page, backtick immediately cancels any unsaved edit
  and returns to the pet page. BLE passkey entry retains higher priority.
- Windows `1.2.0` delivers the core Machine Agent. Windows BLE HID uses the
  operating system's native support. Windows virtual microphone and Unicode
  GATT injection are explicitly deferred.
- The existing Swift/macOS Agent is retained rather than replaced by a
  cross-platform rewrite.
- The final macOS uninstall is a complete purge, including configuration and
  logs, and occurs only after build and local verification are complete.

## Alternatives Considered

### Device-led onboarding plus a separate Windows Agent

This is the selected design. It meets the requirement that Wi-Fi be selected
and entered on the Cardputer, leaves the stable macOS BLE/audio implementation
unchanged, and allows a pure-Go Windows core to be cross-compiled on the
current Mac.

### Browser-led provisioning

The device would expose a temporary access point and collect Wi-Fi credentials
in a browser. This is simpler but does not meet the required on-device
selection and password-entry workflow.

### Unified macOS and Windows Agent rewrite

A common cross-platform core could reduce future duplication, but it would
place the already working macOS CoreBluetooth, Core Audio, and HAL paths at
unnecessary risk during the public-release milestone.

## Security Audit and Public Release Boundary

### Current audit result

The read-only pre-design audit covered:

- all six local branch tips;
- all reflog-reachable commits;
- every currently retained unreachable commit and blob reported by
  `git fsck`;
- historical file names and object contents;
- common private-key, token, credential-file, URL-userinfo, Wi-Fi password,
  environment-file, and private-firmware indicators.

No real Wi-Fi credential, pairing secret, private key, access token, populated
environment file, private NVS image, or private full firmware image was found
in the retained Git object database. Matches were limited to deliberate test
fixtures, field names, redaction tests, and synthetic passwords.

No history rewrite is justified by the evidence. Rewriting clean history would
invalidate commit identities without removing a demonstrated secret. If a
later gate finds a real credential, public publication stops; the credential
must be rotated first, and only then is the exact affected history rewritten.

### Local private artifacts

The working machine currently contains ignored private artifacts, including a
private full image and a generated Wi-Fi NVS image. They are not Git history,
but they are forbidden from the public release bundle and must be removed
during final cleanup.

### Repeatable security gate

The release tooling gains a deterministic, non-secret-printing audit that:

1. enumerates all refs, reflogs, and retained unreachable objects;
2. scans historical paths and blobs for credential and private-artifact
   indicators;
3. reports only object/path/rule metadata with candidate values redacted;
4. rejects private output directories and credential-bearing firmware from
   the public artifact set;
5. rejects internal-only URLs, populated secret configuration, and release
   files outside the manifest;
6. verifies that every published artifact is declared with a SHA-256 digest.

The public workflow produces only generic application and full images. Private
Wi-Fi provisioning is removed from the default release instructions and is
not invoked by the public release script. Only the reviewed release branch and
version tag are eligible for publication; existing experimental branches are
not public release inputs.

## Firmware Onboarding

### Persistent model

Firmware stores a small versioned onboarding record in NVS:

- schema/onboarding version;
- last completed verified step;
- overall completion flag;
- timestamps or counters needed for retry diagnostics.

It does not duplicate the Wi-Fi password. Existing Wi-Fi storage remains the
sole credential owner. The overall completion flag is written only after all
three gates pass.

Upgrades from an already configured legacy firmware must not force onboarding
merely because the new key is absent or because BLE/Agent is temporarily
offline. A one-time migration marks onboarding complete when an existing,
valid pre-`1.2.0` product configuration proves the device was already
commissioned. Only a blank/factory-reset configuration enters the new
first-run flow. Explicitly selecting "Run setup again" remains the way to
recommission an upgraded device.

### State machine

```text
BOOT
  -> WIFI_SCAN
  -> WIFI_SELECT
  -> WIFI_PASSWORD
  -> WIFI_CONNECT_VERIFY
  -> BLE_PAIR_GUIDE
  -> BLE_PAIR_VERIFY
  -> AGENT_INSTALL_GUIDE
  -> AGENT_HEARTBEAT_VERIFY
  -> COMPLETE
  -> PET
```

Power loss resumes from the last verified step. A transient post-onboarding
Wi-Fi, BLE, or Agent outage does not clear completion or restart onboarding.
Those failures remain normal status-page conditions.

### Wi-Fi step

- The Cardputer scans networks asynchronously so the UI and watchdog remain
  responsive.
- Results are paginated and show SSID, signal level, and security state.
- `；` and `。` move the selection, Enter confirms, and explicit items allow
  rescanning or entering a hidden SSID.
- Password entry occurs on the hardware keyboard. Characters are masked;
  backspace edits locally; no HID event is emitted.
- Firmware attempts the connection before persistence. Success requires a
  usable station connection and assigned IP.
- A failed attempt shows a bounded, human-readable reason and returns to retry
  without overwriting the last known-good stored configuration.
- Wi-Fi credentials never appear in firmware logs, Web responses, crash
  diagnostics, or test snapshots.

### BLE step

- The screen presents the single public name `Cardputer Codex` and tells the
  user to initiate pairing in the computer's Bluetooth settings.
- Passkey input from the Cardputer has priority over onboarding navigation,
  backtick, Settings navigation, and HID output.
- Completion requires a persisted bond and a live HID connection, rather than
  merely observing an advertising or pairing callback.
- Reboots preserve the bond and use the existing reconnect behavior.

### Machine Agent step

- The screen and setup Web page show the Cardputer IP, setup address, device
  PIN, platform-specific installation guidance, and public release download
  entry.
- Download URLs derive from a public release manifest/base URL at packaging
  time. Firmware source does not contain an internal hostname or local path.
- Before a public URL exists, the same release bundle supports offline testing
  from `dist/`; this does not change the device protocol.
- The step completes only after a PIN-authenticated Agent heartbeat succeeds.
  There is no user-operated "pretend complete" button.

### Recovery

- During onboarding, backtick cancels the current local input or returns to
  the prior onboarding step because no normal pet page is available yet.
- After onboarding, Settings includes a confirmed "Run setup again" action.
  It resets onboarding progress but does not erase data until the user
  explicitly confirms the relevant Wi-Fi, BLE, or Agent reset operation.
- Factory reset remains separately guarded and is not overloaded onto ordinary
  navigation.

## Navigation and Input Routing

The keyboard dispatch order becomes:

1. active BLE passkey entry;
2. active onboarding input;
3. global non-pet backtick return;
4. current Settings editor/menu handling;
5. page navigation;
6. pet-page HID/action output.

On any completed-installation page other than the pet page, pressing backtick
immediately discards unsaved local edits and returns to the pet page. This
includes Settings root, Settings submenus, edit forms, result dialogs, Device,
Codex, and Sync pages.

No key from a normal non-pet page is emitted as HID. The pet page remains the
only normal page that forwards configured keyboard actions.

## Windows Machine Agent

### Platform and packaging

The Windows Agent is a separate Go module and protocol peer:

- supported baseline: Windows 10 22H2 and Windows 11;
- normal installer: NSIS per-user x64 installer;
- portable artifacts: `windows-amd64` and `windows-arm64` executables;
- installation root: `%LOCALAPPDATA%\\Cardputer Codex Companion`;
- configuration/log root: the corresponding per-user local application data;
- login startup: a per-user Scheduled Task;
- uninstallation: Start Menu entry and `uninstall.exe`.

The normal installation does not require Administrator privileges. No kernel
driver or Windows service is part of `1.2.0`.

### Responsibilities

The Agent:

- discovers or accepts the address of a Cardputer on the local network;
- performs PIN authentication and heartbeat reporting;
- launches and supervises `codex app-server --listen stdio://`;
- reports active session, model, thinking level, Fast state, and available
  rate-limit fields;
- accepts supported Codex actions from the device;
- reads the configured Codex pet, renders it, encodes the existing CCPT v1
  format, and synchronizes it through the authenticated LAN API;
- reconnects with bounded backoff and rotates bounded local logs;
- exposes a local diagnostic/status command without exposing secrets.

BLE keyboard reports remain native OS HID and bypass the Agent.

### Pairing and secret storage

The Agent first attempts local discovery and falls back to manual device IP.
Initial pairing uses the device PIN. It records the device certificate
fingerprint after the authenticated first connection and requires that
fingerprint on later connections.

The PIN and related pairing material are protected with Windows DPAPI. They
are never stored as plaintext JSON, included in logs, command-line arguments,
diagnostic bundles, or installer properties.

### Deferred Windows capabilities

The Windows `1.2.0` installer and documentation explicitly mark these as not
included:

- Cardputer BLE microphone to a Windows virtual input device;
- Unicode string injection over the custom BLE GATT path.

Neither limitation affects ordinary BLE HID keys and key combinations.

### Compatibility proof

A versioned LAN/CCPT protocol contract and shared fixtures prove:

- request/response field and authentication compatibility;
- heartbeat and action behavior;
- rate-limit omission when a value is unavailable;
- deterministic pet rendering and CCPT byte output;
- reconnect/backoff and configuration migration;
- log and diagnostic redaction.

The Mac can cross-compile and inspect the Windows binaries and installer.
Actual Windows installation, login startup, uninstall, Codex integration, and
device heartbeat remain a required real-Windows HIL gate; cross-compilation is
not reported as runtime proof.

## Web Behavior

Before onboarding completes, the Web UI exposes only:

- current setup step and progress;
- Wi-Fi/BLE/Agent status required for setup;
- platform installation guidance and release download links;
- bounded diagnostics that do not reveal credentials.

Normal keyboard/profile configuration remains unavailable until setup
completes. After completion, the existing PIN login and configuration UI
remain in place, with a guarded "Run setup again" action in Settings.

## Versioning and Artifacts

All live version surfaces advance together to `1.2.0`, including firmware,
macOS components, Windows Agent, installers, manifests, Web-visible version,
and consistency tests.

Expected public artifacts include:

- generic firmware application image;
- generic full flash image;
- macOS installer package;
- Windows x64 NSIS installer;
- Windows amd64 and arm64 portable archives;
- SHA-256 manifest and machine-readable release manifest;
- public installation, onboarding, uninstall, and capability documentation;
- redacted Git-history security audit report;
- Windows HIL checklist/report template.

No private NVS image, private full image, local configuration, device PIN,
Wi-Fi credential, signing secret, or internal-only URL may appear in the
artifact directory or manifest.

## Verification Strategy

Implementation follows test-driven development. Each behavior begins with a
focused failing test.

Firmware tests cover:

- clean first boot and upgrade migration;
- Wi-Fi scan, pagination, hidden-network selection, masked input, retry,
  connect-before-persist, and rollback;
- BLE bond/HID verification and passkey priority;
- authenticated Agent heartbeat as the final completion gate;
- reset/resume behavior across every onboarding step;
- backtick behavior and zero HID leakage from non-pet pages;
- secret-free logs and Web payloads;
- version and resource/stack constraints.

Agent and protocol tests cover:

- Windows configuration and DPAPI abstraction;
- Codex app-server supervision and telemetry mapping;
- LAN authentication, certificate fingerprinting, retry, and heartbeat;
- action routing and unavailable-rate-limit omission;
- byte-compatible pet synchronization;
- installer install/startup/uninstall scripts;
- secret redaction and bounded logs.

Release gates cover:

- Python and native host tests;
- sanitizer host tests;
- clean ESP-IDF/PlatformIO firmware build;
- Swift/macOS regression and packaging tests;
- Go unit/integration tests plus Windows cross-build;
- NSIS installer construction and static inspection;
- version consistency, SHA-256 verification, artifact allowlist, and complete
  Git-history security scan.

## Final Local Cleanup

Cleanup starts only after all Mac-based build and verification gates pass.
Using the project uninstall path, it removes:

- the installed macOS Agent application;
- Agent and helper LaunchAgents;
- the Core Audio HAL plug-in and AudioBridge helper;
- running helper/Agent processes;
- pairing configuration, cached device state, and logs;
- ignored private Wi-Fi and private firmware artifacts in the project.

Post-cleanup checks prove the launch jobs are absent, processes are stopped,
the HAL and helper are absent, the user configuration/log directories are
absent, and private artifact paths are empty. The Agent is not reinstalled
after this check, preserving a true first-install environment for the new
device E2E.

## Acceptance Criteria

- The repeatable audit passes for every retained branch, reflog, unreachable
  object, and public artifact without exposing candidate secrets.
- No demonstrated credential requires history rewriting; any future real
  finding blocks publication and triggers rotation plus targeted cleanup.
- A clean device completes Wi-Fi selection/password input, computer-initiated
  BLE pairing, and authenticated Agent installation guidance entirely through
  the defined state machine.
- Onboarding resumes safely after reboot and completes only with all three
  verified gates.
- Backtick returns immediately to the pet page from every normal non-pet page,
  and no non-pet keystroke reaches HID.
- Windows Agent artifacts build reproducibly, pass protocol/pet/installer
  tests, and are clearly labeled pending final Windows HIL until that gate is
  run.
- All `1.2.0` public firmware, macOS, and Windows artifacts are version
  consistent, checksummed, and contain no private provisioning data.
- The current Mac ends with the Agent, audio driver, helpers, user
  configuration, logs, and private project artifacts completely removed.

## Explicit Boundaries

- No public server deployment or repository publication is performed without
  a separately identified destination and authorization.
- No Windows virtual audio driver or Unicode GATT implementation is included
  in `1.2.0`.
- No existing experimental branch is pushed as part of the public release.
- No Git history is rewritten without a demonstrated secret and an exact
  remediation plan.
- No device flash is required for this preparation task unless the user later
  requests it; the deliverable is the new generic firmware ready for clean
  new-device E2E.
