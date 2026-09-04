Mnemonic3D Recommended Printer Enhancements
============================================

These are optional, on top of the mandatory Orca MultiACE patches.
Checked by default, but you can uncheck the whole set on the previous screen.

1. Stock-motion unload cleanup
-------------------------------
Runs the firmware's existing ROUGHLY_CLEAN_NOZZLE_WITH_DISCARD routine
after the normal tip-form step: one additional pass for a direct-feed
toolhead, or five rapid passes when the channel is ACE-routed. The extra
ACE passes help knock loose filament left clinging after a purge. Every
pass uses the material's clean_nozzle_temp value.

This change adds no extrusion or retraction and defines no custom movement,
distance, speed, purge push, fan choreography, transport, sensor check, or
retry sequence beyond repeating the existing macro. Those remain the stock
multiACE 0.99.8b behavior.

2. Material nozzle-wipe temperatures
-------------------------------------
The approved nozzle-wipe temperatures are applied in memory to the stock
filament parameter database:

  - PLA / PLA-CF ........... 200C
  - TPU variants ........... 220C
  - PETG family ............ 240C
  - ABS / ASA .............. 250C
  - PA family (Nylon/CF/GF) 270C
  - PC family .............. 280C
  - PVA / unknown fallback . 170C

The database retains the stock material/vendor/subtype entries and all stock
flow, pressure, soft-filament, and calibration data. No copied placeholder
TPU 90A, generic TPU-HF, or BVOH mechanics are added.

3. Tip forming remains stock
----------------------------
The installer does not replace the tip-form engine and does not patch the
printer's [ace_tipform] section. A printer using stock U1 1.6 therefore keeps
its exact stock U1 1.6 behavior. The installer also does not replace ACE sensor
handling, transport, retract, retry, purge, fan, or cooling choreography.

Why these are recommended rather than mandatory
-------------------------------------------------
None of these temperature and wipe changes are required for Orca Slicer
compatibility. The mandatory group contains the panel, Auto-Sync, homing,
parking, and related integration fixes. This optional group can be skipped
to leave stock temperature and wipe behavior untouched.

Touchscreen support installed with Mnemonic3D filaments
--------------------------------------------------------
When the optional Mnemonic3D calibrated-filament catalog is selected, the
installer also selects the compatible touchscreen-support group. It preserves
the normal color and tool-number display while adding a white ACE badge with a
black outline to whichever toolhead is dynamically assigned to an ACE. The
catalog includes Mnemonic3D TPU for ACE as hard ACE-compatible 68D TPU with a
230 C nozzle, 45 C bed, and 1.26 g/cm3 density baseline. The touchscreen's
fixed 24-slot table retains all 15 Mnemonic3D choices and eight legacy Generic
rows; PHA is displaced. Snapmaker and Polymaker remain unchanged.
same touchscreen support keeps the loaded-color change confirmation and uses
output-only projections for ACE-owned TPU; it does not rewrite saved filament
parameters.
