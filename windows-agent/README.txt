Cardputer Codex Companion Windows Machine Agent

Install:
  Use the x64 installer, or extract the portable archive.

First pairing:
  cardputer-agent.exe pair

The Agent prompts for the Cardputer HTTPS address and masks the eight-digit
device PIN. Pairing material is protected with Windows DPAPI for the current
user. It is never accepted as a command-line argument.

Commands:
  cardputer-agent.exe status
  cardputer-agent.exe doctor
  cardputer-agent.exe run
  cardputer-agent.exe --version

Windows 1.3.3 supports Codex status/actions, LAN onboarding, and pet sync.
Bluetooth keyboard input is handled natively by Windows. Bluetooth microphone
and Unicode injection over the companion GATT channel are not included in this
release.
