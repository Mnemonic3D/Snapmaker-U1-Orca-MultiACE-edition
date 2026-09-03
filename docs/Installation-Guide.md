# Installation Guide

Steps for installing Orca Slicer - MultiACE Edition and the Mandatory Patch
against a genuinely stock-flashed Snapmaker U1 MultiACE (firmware 0.99.8b).
Follow them in order.

> **Physically wiring and setting up your ACE units is out of scope here.**
> For real-time, up-to-date instructions on properly setting up ACEs, see
> [decay71/multiACE](https://github.com/decay71/multiACE).

Something on your printer looking off after following these steps? See
[Troubleshooting Commons](Troubleshooting-Commons.md) — most of what looks
"broken" right after install is expected, documented behavior.

## Step 1. Make sure your printer has 0.99.8b installed

- Get `U1_1.5.2-paxx12-21_multiACE0.99.8b` from [Mult1ACE - Multi ACE Pro Software for Snapmaker U1](https://postapocalyptic-diy.com/mult1ace-multi-ace-pro-software-for-snapmaker-u1/)
- Extract the `.bin` and copy it to the root of a FAT32 USB drive
- Touchscreen: `Settings > About > Firmware Version > Local Update` → select the file → confirm, let it reboot
- The Mandatory Patch's file baselines are built against genuine stock 0.99.8b — installing over anything else gets flagged as "locally modified"

![Firmware Version screen — "LAN Mode is on, No internet connection, Can't update online" with the Local Update button](images/troubleshooting/firmware-local-update.jpg)

## Step 2. Install Orca Slicer - MultiACE Edition

- Go to this repository's [Releases page](https://github.com/Mnemonic3D/Snapmaker-U1-Orca-MultiACE-edition/releases) and download the latest Setup installer
- Run it and complete the installer
- Launch **Orca Slicer - MultiACE edition**

## Step 3. Install the patch onto the printer

1. **Enable Root Access** — Touchscreen: `Settings > Maintenance > Root Access` → scroll down and Agree → `Open`. Credentials: `root` / `snapmaker` (this is what the installer uses over SSH).

   ![Root Access screen after granting access](images/troubleshooting/root-access-granted.jpg)

2. **Open the Mandatory Patch installer and connect** — run `MultiACEPatchesInstaller.exe`, enter the printer's IP address (shown on the touchscreen), click Connect. The IP field is always blank by default.

3. **Review and Apply** — the installer verifies every file against known stock hashes (a genuinely stock printer should show `OK` across the board), then click **Apply Patch**.

4. **After "Patch Installed!" — do all four, in order:**
   1. **Full power cycle the printer** — power off completely, wait ~10 seconds, power back on. A soft restart from a menu is not the same thing; most of what was just written only takes effect, and only persists, after a real reboot.
   2. **Reconnect Wi-Fi** if it doesn't reconnect on its own (`Settings > Network`) — the first time Root Access is enabled after a flash, the connection can go stale.
   3. **Ensure all filaments are set on the printer**, then **ensure the MultiACE Web Preflight page is set up to your needs** (mode, Feeder/ACE assignment per toolhead) **and filaments are set there too** — every toolhead's confirmed filament source is intentionally cleared on restart (see [Troubleshooting Commons](Troubleshooting-Commons.md)), so this has to be redone after every reboot, not just checked once.
   4. **Do one last reboot** to confirm everything you just set actually holds and persists correctly.
