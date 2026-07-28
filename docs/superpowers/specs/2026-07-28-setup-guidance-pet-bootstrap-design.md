# SETUP Guidance and Pet Bootstrap Design

## Goal

Make first-run SETUP self-explanatory from BLE pairing through Machine Agent
installation, retain a final on-device handoff until the user presses a key,
and make both Machine Agents synchronize the selected Pet immediately when a
device requests its first snapshot.

## Confirmed Product Decisions

- The post-Agent completion guide is transient. The successful Agent
  checkpoint is persisted immediately, but a reboot before acknowledgement
  skips the guide and opens the Pet page.
- The BLE advertised and pairing name shown to users is exactly
  `Cardputer Codex`.
- SETUP remains local-only input. No SETUP key press may reach BLE HID.
- The public release is Factory `1.3.1` plus M5Launcher-compatible
  `1.3.1l`.
- The release includes the connected-device flash, public `main`, GitHub
  Release assets, and the GitHub Pages Web Serial installer.

## Firmware State Model

Add `complete_guide` to `OnboardingStep` without adding a stored checkpoint or
changing the encoded record schema.

When an authenticated Agent heartbeat arrives during `agent_install_guide`,
the state machine:

1. persists `OnboardingCheckpoint::complete`;
2. leaves the in-memory step at `complete_guide`;
3. keeps onboarding input capture active;
4. renders the completion guide.

The first pressed physical key acknowledges the guide and changes the in-memory
step to `complete`. This acknowledgement performs no storage write. The
existing UI transition then opens the Pet page. A reboot reloads the already
persisted `complete` checkpoint and therefore opens the Pet page directly.

If the persistence write fails, SETUP remains on the Agent page and continues
waiting. It must not display completion guidance for a checkpoint that was not
saved.

## On-Device Copy

All guidance uses the SETUP-only 1x body font and must fit the five visible
rows. Normal DEVICE, CODEX, SYNC, and SETTINGS fonts remain unchanged.

### BLE Pairing

The BLE step communicates the complete initiation path:

1. `SETUP 2/3 BLUETOOTH`
2. `ON COMPUTER: BLUETOOTH`
3. `SEARCH: CARDPUTER CODEX`
4. `SELECT PAIR / CONNECT`
5. `TYPE COMPUTER CODE HERE`

The existing Cardputer passkey editor remains authoritative after the computer
starts pairing.

### Machine Agent

The Agent step remains self-contained:

1. `SETUP 3/3 AGENT`
2. `IP:<device-ip> PIN:<device-pin>`
3. `MAC: ./install.sh install`
4. `WIN: RUN 1.3.1 SETUP.EXE`
5. `WAITING HEARTBEAT...`

The PIN is shown only on the Cardputer. No PIN is added to logs, process
arguments, installer packages, or public artifacts.

### Completion Handoff

After the authenticated heartbeat:

1. `SETUP COMPLETE`
2. `SETTINGS: FN+/ X4`
3. `WEB: HTTPS://<device-ip>/`
4. `PIN:<device-pin>`
5. `PRESS ANY KEY`

The first key is consumed only as acknowledgement. Its press and release must
not navigate, edit Settings, trigger a macro, or generate a HID report.

## Immediate Pet Bootstrap

The existing 30-second successful cadence and 5-second error cadence remain
unchanged.

For each Mac or Windows Agent loop:

1. record whether the normal cadence attempted Pet synchronization in this
   loop;
2. poll the device heartbeat/action endpoint;
3. if `needs_snapshot` is true and no Pet attempt occurred in this loop,
   synchronize Pet immediately;
4. construct and post the requested snapshot with the newest Pet ID/digest.

This covers a freshly installed device and a device that restarts inside an
existing Agent's 30-second cadence. It also avoids uploading the same Pet twice
when the Agent's initial cadence attempt and `needs_snapshot` occur together.

Pet synchronization errors remain non-fatal to the main Agent loop. They
publish the existing error result and schedule the existing five-second retry.

## Compatibility and Security

- `OnboardingRecord` remains schema version 1 and 12 bytes.
- Existing stored `complete` records retain their numeric value and behavior.
- Wi-Fi credentials, PINs, BLE bonds, Profiles, Pets, and device settings are
  preserved by app-only upgrades.
- Factory images remain generic and contain no Wi-Fi credentials, fixed PIN,
  BLE pairing material, private certificate, or cached Pet.
- Launcher artifacts retain the M5Launcher partition/storage contract.
- No new cloud or off-LAN control path is introduced.

## Testing

Use test-first development.

Firmware host tests must prove:

- BLE and Agent copy contains the exact actionable guidance;
- authenticated heartbeat enters `complete_guide` only after a successful
  checkpoint commit;
- any key acknowledges the transient guide;
- press and release are captured;
- reboot after the heartbeat skips the transient guide;
- stored-record schema and legacy completed values remain compatible.

Mac tests must prove:

- initial cadence still synchronizes immediately;
- `needs_snapshot` inside a non-due cadence forces one immediate sync;
- a due cadence plus `needs_snapshot` performs only one sync.

Windows tests must prove the same three Pet bootstrap cases.

Release verification must include:

- focused C++/Swift/Go tests;
- normal and sanitizer firmware host suites;
- clean ESP-IDF Factory and Launcher builds;
- public firmware and partition checks;
- macOS and Windows Agent build/package gates;
- release checksum verification;
- all-history credential/private-artifact audit;
- attached-device flash verification and serial boot observation;
- live Pages manifest and Factory SHA-256 verification.

## Deployment and Acceptance

Publish Factory `1.3.1`, Launcher `1.3.1l`, updated Agent artifacts, checksum
manifest, self-contained Web Installer ZIP, GitHub Release, and GitHub Pages.

Acceptance requires:

- the connected Cardputer boots as `1.3.1` without panic or reboot;
- SETUP text renders at 1x and all three new guidance states are reachable;
- final acknowledgement opens the Pet page without producing HID;
- a fresh `needs_snapshot` causes immediate Pet availability;
- public release and Pages hashes match the locally verified artifacts.
