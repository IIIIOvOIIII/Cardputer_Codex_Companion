# SETUP Agent Guidance and Installer IP Input Design

Date: 2026-07-28  
Target firmware: `1.2.2`

## Goal

Make first-run setup readable and self-sufficient without changing the text size
of the normal DEVICE, CODEX, SYNC, or SETTINGS pages:

- render SETUP body text at a smaller size;
- show the Cardputer LAN IP and current device PIN while setup waits for the
  Machine Agent heartbeat;
- ask for a Cardputer IP address, rather than a URL, in the interactive macOS
  installer;
- compile and verify a versioned firmware artifact.

## Confirmed Product Boundary

- Only `UiPage::onboarding` uses the smaller body text size.
- All non-SETUP text pages keep the current `2x` body text size.
- The Agent setup step renders these five rows:
  1. `SETUP 3/3 AGENT`
  2. `IP:<current IPv4>`
  3. `PIN:<current eight-digit PIN>`
  4. `RUN ./install.sh`
  5. `WAITING HEARTBEAT...`
- The interactive macOS installer accepts only an RFC1918 IPv4 address and
  normalizes it to `https://<address>` internally.
- Existing JSON configuration remains backward compatible with validated HTTPS
  RFC1918 URLs and `.local` hostnames.
- No DEVICE, CODEX, SYNC, SETTINGS, web UI, BLE, or Agent protocol behavior is
  redesigned by this change.

## Root Cause

`display_render_page()` currently applies `kDisplayBodyTextSize` to every
non-pet page. SETUP therefore inherits the same `2x` body scale intended for
normal status and settings pages.

`OnboardingController::content()` owns the third-step text but has no runtime
device identity input, so it cannot include the current Wi-Fi address or pairing
PIN.

`interactive_config()` currently reads a complete HTTPS URL. The installer
already validates LAN URLs, but the public first-run workflow tells the user to
copy the IP shown on the device.

## Design

### Page-specific text sizing

Add a SETUP-specific body text constant and select the body size from
`model.page()` inside `display_render_page()`. The common page title and all
non-SETUP body rendering remain unchanged. The SETUP visible-row limit stays at
five, matching the fixed Agent guidance and preserving existing Wi-Fi selection
scrolling.

### Runtime Agent guidance

Extend onboarding content generation with optional runtime IPv4 and PIN inputs.
Only `OnboardingStep::agent_install_guide` consumes them. The product controller
passes `product_wifi_ipv4()` and `product_web_pairing_code()` when refreshing
the onboarding UI.

This keeps hardware access outside the state machine and leaves the content
generator host-testable. Values are copied into bounded `std::string` rows
before the UI mutex is acquired, preserving the current lifetime and locking
model.

### Installer normalization

Add a focused helper that:

1. strips surrounding whitespace;
2. parses an IPv4 address;
3. rejects IPv6, public, loopback, link-local, and otherwise non-RFC1918 input;
4. returns `https://<address>`.

`interactive_config()` uses the helper and changes its prompt to
`Cardputer IP: `. `validate_config()` and `validate_device_url()` remain the
source of truth for stored/config-file compatibility.

### Version and artifacts

Advance the firmware-visible version from `1.2.1` to `1.2.2`. Update live
version contracts required by the repository's release consistency tests, while
leaving historical release documents unchanged. Build the app-only and full
firmware images and expose their paths under `dist/`.

## Verification

- Host test proves Agent guidance includes the supplied IP and PIN.
- Structural display test proves SETUP selects its smaller body size and the
  normal pages still use `kDisplayBodyTextSize = 2`.
- Installer test proves an RFC1918 IP is normalized and the prompt no longer
  asks for a URL.
- Installer negative tests reject non-RFC1918 and non-IPv4 input.
- Firmware host tests and targeted Python tests pass.
- ESP-IDF build completes for version `1.2.2`.
- Produced firmware metadata and checksums are inspected before delivery.

