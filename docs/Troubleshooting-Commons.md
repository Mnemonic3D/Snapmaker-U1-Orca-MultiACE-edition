# Troubleshooting Commons

Common confusions and fixes, gathered from a live install-and-test pass
against a genuinely stock-flashed Snapmaker U1 MultiACE. If you haven't
installed yet, start with the [Installation Guide](Installation-Guide.md).

## 1. A toolhead's ACE dropdown shows blank

**Why:** Each toolhead defaults to its own ACE index (head 0 → ACE 0, head 1 → ACE 1, etc.) until something saves a real mapping. A single-ACE printer only has ACE 0, so heads 1–3's dropdowns are bound to an ACE that doesn't physically exist and render blank. Head 0 looks fine purely by coincidence.

**Fix:** Open the ACE dropdown on the affected toolhead and select the only ACE listed (usually `ACE 1`). The mapping saves immediately and survives future reboots.

## 2. Every filament circle shows a gray "?"

**Why:** Right after a fresh flash and install, no toolhead has a known source yet — nothing has been loaded or confirmed since boot. This is the correct empty state.

**Fix:** Load filament into each toolhead you plan to use. The icon fills in the moment a load is confirmed.

## 3. Materials show "/" right after a reboot

**Why:** The touchscreen process starts before Klipper and the ACE unit finish reporting state — a startup race, not a bug.

**Fix:** Wait 10–15 seconds after the home screen first appears. If it's still blank, back out to Home and back in once.

## 4. Dashboard says "Unknown source" even though the touchscreen says Loaded

**Why:** The sensors know filament is physically present (that's what the touchscreen's "Loaded" reflects), but the software record of *which ACE slot fed it* is deliberately dropped on every restart — a fail-closed safety choice, the same philosophy behind reviewed-print-start refusing to trust stale state. The two screens are both telling the truth about different things.

**Fix:**
1. On the Dashboard, click `Load` on the ACE slot that's actually in the toolhead
2. Confirm the pre-selected slot in the dialog (correct it if it's wrong)
3. Click **Unload, then load** — if the slot already matches what's loaded, nothing physically moves; it just re-records the source

## 5. Wi-Fi icon says connected, but it isn't

**Why:** This happens when the printer was only soft-restarted (from a menu) instead of a full manual power cycle. A soft restart can leave Wi-Fi in a stale state where the status icon keeps reporting "connected" even though it isn't.

**Fix:**
1. `Settings > Network` → open the Wi-Fi entry and re-enter the password (or toggle Wi-Fi off/on)
2. If the interface itself is glitching and won't respond, fully power-cycle the printer — not a soft restart

## 6. Verifying everything is actually correct

Open the MultiACE web dashboard:
- Every toolhead you intend to print with should read a real material, not "Unknown source"
- Mode should match your setup (`normal` / `multi` / `head`) — set on the Config page
- ACE routing (the small `ACE` badge) should only appear on toolheads actually fed by an ACE, and only in `multi` or `head` mode — never in `normal` mode

![Config page — Mode selector and Reboot printer button](images/troubleshooting/config-page.jpg)

One example of a correctly resolved setup (yours may look different — this printer happens to be in `head` mode with one ACE-routed toolhead and three Feeders, but any mix of ACE-routed and Feeder heads, or `multi` mode entirely, is equally valid): every toolhead shows a real material and source, Feeder heads are checked and show their loaded filament, and each ACE-routed head shows its confirmed slot instead of "Unknown source."

![Example dashboard with every toolhead's source resolved — one valid layout among several](images/troubleshooting/dashboard-healthy.jpg)

If something still looks wrong after checking against the items above, it's worth reporting as a real issue rather than assuming user error.
