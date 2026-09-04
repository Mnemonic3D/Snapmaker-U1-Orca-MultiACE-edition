# Snapmaker U1 - multiACE Patch Installer

A Windows GUI updater that logs into the printer over SSH, proves the printer
is the build the patch was made for, and only then applies the fixes.

```
python u1_patch_installer.py
```

---

## What it does

**Screen 1 - Connect.** Enter the printer's IP address and the SSH password.
The IP address is intentionally blank for every new printer session. The stock
multiACE SSH port, username, and password are pre-filled in memory and are never
saved as user-specific settings.

**Screen 2 - Verify.** Before writing anything, the installer:

- reads the firmware build and the `MULTIACE_VERSION` out of `ace.py`, and
  checks both against what the patch declares it requires;
- SHA-256 hashes every file the patch intends to replace, in one round trip,
  and sorts each into one of four verdicts:

  | Verdict | Meaning | Result |
  |---|---|---|
  | **Ready to patch** | matches a known pre-patch original | will be updated |
  | **Already current** | already holds the patched content | skipped |
  | **Missing on printer** | not present | blocks, unless the file is marked `allow_create` |
  | **Modified / unknown** | matches nothing the patch knows about | **blocks** |

  The **Apply** button stays disabled unless the build gate passes and no file
  blocks. A "Modified / unknown" file means something else edited it, so the
  patch refuses rather than clobbering work it cannot account for.

  An override checkbox appears when verification fails. It still takes a full
  backup first, and it still asks for confirmation.

**Screen 3 - Apply.** In order:

1. Create `/home/lava/u1-patch-backups/<patch-id>-<timestamp>/` on the printer.
2. Copy every file it is about to touch into that directory and **re-hash each
   backup to confirm it is readable** - if a backup does not verify, the run
   aborts before a single file is written.
3. Also scan older backup manifests for legacy files that belonged to earlier
   versions of this same patch but are not in the current file list. Those stale
   files are backed up, then removed so upgrades do not keep obsolete patch
   changes. If a candidate file currently matches a known pre-patch baseline
   hash from an older install, cleanup leaves it alone as user/preservation
   state.
4. Write `restore.json` and a standalone `restore.sh` into the backup directory,
   so a rollback never depends on this PC.
5. Upload each file atomically (staged, then moved), re-hashing after the move
   to confirm it landed. Any failure rolls the whole set back automatically.
6. Restart the Klipper process, the multiACE web service, and the touchscreen
   supervisor when their tracked files changed.

After every successful installation, the Apply screen keeps a persistent
**REBOOT REQUIRED** notice visible and also opens the same notice as a modal.
Two actions appear directly underneath it:

- **Go back add new printer to patch** closes the completed printer's SSH
  connection, clears its report, consent, selected options, log pointer, and
  rollback pointer, blanks the IP field, and returns to Connect for the next
  printer.
- **Close** exits the installer.

The reset never deletes the completed transcript or the backup stored on the
printer. Each printer gets a separate uniquely named transcript. The full
printer reboot remains mandatory before using every successfully patched
printer, regardless of which completion action is selected.

File ownership and permissions are inherited from whatever the file already had
unless the manifest pins them explicitly, so a patch never re-owns a Klipper
module out from under the `lava` user.

Every run also writes a transcript to `logs\patch-<timestamp>.log`.

### Rolling back

The **Roll back this patch** button restores everything from the backup taken
at the start of the run. If this PC is not around, do it on the printer:

```bash
sh /home/lava/u1-patch-backups/<patch-id>-<timestamp>/restore.sh
```

---

## What is in the current patch

Defined by `sources\patch_sources.json`, built into `payload\manifest.json`.

| File on the printer | What it brings |
|---|---|
| `/home/lava/klipper/klippy/extras/AFC_lane.py` | dynamic T-map, lane display fix, panel completion sync |
| `/home/lava/klipper/klippy/extras/AFC_unit.py` | hide empty lanes in the panel |
| `/home/lava/printer_data/config/extended/klipper/afc.cfg` | macros for the synced panel behaviour |
| `/home/lava/printer_data/config/tools/post_process_virtual_toolheads.py` | current virtual-toolhead post-processor |
| `/home/lava/mainsail/assets/index-CIsGge_F-afc-responsive.css` | responsive Mainsail AFC panel |
| `/home/lava/klipper/klippy/extras/print_task_config.py` | output-only ACE-owned TPU status projection plus a dynamic ACE toolhead marker; saved filament settings remain unchanged |
| `/usr/bin/gui` | native loaded-color confirmation and a dynamic white `ACE` badge over the assigned toolhead's color circle; stock- and Mnemonic3D-catalog variants are selected automatically |

Gated to firmware `1.5.2*` and multiACE `0.99.8b`.

`ace.py` and the other current MultiACE files are now included from the guarded
authoritative sources named in `sources\patch_sources.json`.

### Warranty and risk acknowledgment

This is an unofficial modification and is not provided, approved, or supported
by Snapmaker. If the patch causes or contributes to a failure, the manufacturer
may deny warranty coverage for that failure, subject to applicable law. Before
Apply is enabled, the portable installer requires the user to acknowledge the
risks of malfunction, data loss, print failure, and property damage, and to
accept responsibility for choosing to install and use the patch. The
acknowledgment also states that, to the maximum extent permitted by applicable
law, the user will not hold the developer known as Mnemonic3D liable for
resulting loss or damage. It does not waive rights or exclude liability that
cannot legally be waived or excluded.

---

## Tools

### `tools\build_payload.py`

Rebuilds `payload\manifest.json` and `payload\files\` from the source spec.
Run it after changing any source file.

```bash
python tools\build_payload.py
python tools\build_payload.py --check    # verify only, write nothing
```

It refuses to produce a file entry with no baselines, since that would make the
installer reject every printer as "modified".

### `tools\capture_baseline.py`

Read-only work against a reference printer.

```bash
# Dry-run the exact gate the GUI will apply, and print the result
python tools\capture_baseline.py --host <printer-ip> --survey

# Make the printer's live copy of a file the patch payload
python tools\capture_baseline.py --host <printer-ip> --capture ace-py

# Record the printer's current hash as an accepted pre-patch state
python tools\capture_baseline.py --host <printer-ip> --baseline afc-cfg
```

`--survey` is the safe way to see what would happen without opening the GUI.

---

## Building the standalone .exe

```powershell
powershell -ExecutionPolicy Bypass -File packaging\build_installer.ps1
```

This installer applies patches onto the printer over SSH - it does not
install anything onto the user's PC - so the build only ever produces a
single-file, standalone/portable `.exe`. There is no Windows-installer-style
setup/uninstall package, and there should never be one.

The build script discovers a per-user Python installation even when it is
not on `PATH`. Check the toolchain without building anything:

```powershell
packaging\build_installer.ps1 -CheckDependencies
```

An explicit override is available when needed:

```powershell
packaging\build_installer.ps1 -PythonExe C:\path\to\python.exe
```

The equivalent environment variable is `U1_PATCH_PYTHON`. If Python is
present but the packaging modules are missing, the script prints the exact
command using `packaging\requirements-build.txt`.

Two stages: rebuild the payload, then run PyInstaller
(`dist\MultiACEPatchesInstaller.exe`, one file, ~18 MB, portable). The
payload is embedded in the `.exe` so it works when just downloaded and
double-clicked; a loose `payload\` folder beside the executable remains an
optional development override.

Useful switches:

```powershell
packaging\build_installer.ps1 -CheckDependencies # verify tool discovery only
packaging\build_installer.ps1 -SkipPayload       # package the payload as-is
```

---

## Requirements

- Python 3.9+ with Tkinter (bundled with the python.org Windows build)
- Python packages from `packaging\requirements-build.txt`

---

## Layout

```
U1 Patch Installer\
  u1_patch_installer.py      GUI entry point
  installer\
    ssh_client.py            paramiko wrapper - exec, sudo, sftp, hashing
    manifest.py              manifest loading + payload hash verification
    integrity.py             the gate: build match and per-file verdicts
    patcher.py               backup, apply, restart, rollback
  sources\
    patch_sources.json       what the patch contains (edit this)
    baselines\               pre-patch originals extracted from git
    current\                 files captured from a reference printer
  payload\
    manifest.json            generated - do not hand-edit
    files\                   generated - mirrors the printer's paths
  tools\
    build_payload.py         spec  ->  payload + manifest
    capture_baseline.py      survey / capture / baseline against a printer
  packaging\
    build_installer.ps1      PyInstaller build pipeline (portable .exe only)
  logs\                      one transcript per run
```
