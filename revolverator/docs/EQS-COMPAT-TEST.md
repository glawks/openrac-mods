# EQS compatibility test — NOT a Revolverator release

This branch is an experimental attempt to make Revolverator 0.4.3 coexist with
[glawks/expanded-quick-select](https://github.com/glawks/expanded-quick-select).
It is not a finished compatibility fix.

## Status

- Source modifications: `revolverator/src/eqs_probe.hpp`,
  `native_weapon_menu.cpp`, `mod.cpp`.
- Windows x64 only: resolves the **already loaded**
  `expanded_quick_select.dll` symbol `openrac_expanded_quick_select_query`
  without adding a dependency or loading the module itself.
- Without the EQS API present, Revolverator keeps its original behaviour.
- When EQS is active, Revolverator avoids complete eight-slot writes to
  **any** of EQS's editor pages, and avoids projecting its one-page registry
  into the native favourites array during level loading, grants, restoration,
  or HUD initialization. It retains page-one logical weapon identity for
  Revolverator's own rendering/selection.
- With EQS installed, Revolverator no longer auto-claims a private quick slot
  on its first purchase, since that slot might hold an EQS gadget. Its page-one
  custom icon and selection now check for the actual Walloper carrier ID 18
  in that native slot before presenting Revolverator.
- The carrier's *global* item icon is kept as native Walloper while EQS is
  managing multiple wheels. Previously equipping Revolverator replaced the
  global icon for native item 18; every page containing a native Walloper then
  appeared to gain Revolverator. The custom icon may still be drawn over an
  explicit page-one logical Revolverator assignment.
- This does NOT yet allow independent Revolverator assignments on pages 2–8.
  A page-specific logical custom-weapon identity and persistence are required
  before custom item ID 65554 can be distinguished from ordinary Walloper ID 18.
- The separate EQS `compatibility-dev` build now tolerates **only** the
  Walloper (60018) / Revolverator (60037) icon pair in HUD layout validation,
  while still requiring all native item IDs to match. Both experimental
  modules need updating to test the equipped-HUD issue.
- Revolverator as an independent **custom weapon on pages 2–8 is not supported**
  by this first experiment. Native item ID 18 is only the Walloper carrier and
  is not Revolverator's logical key `0x10012`.
- The user's previously observed `HUD layout mismatch` may have additional
  causes and remains to be retested.
- This revision also addresses the user-observed case where wheels initially
  work but selecting Revolverator reintroduces broader changes: the shared
  global favourites array must not be repopulated from Revolverator's single
  registry while EQS owns it.
- The fork's `main` and upstream `noire342/openrac-mods` are unchanged.

## Respawn ammo regression (21 November 2026)

A reproducible issue was found after Ratchet dies with Revolverator equipped:
the saved and transported ammo counts remain nonzero (e.g., 13/13), but the
native ammo-capacity metadata at vendor field +14 becomes **0**. Native TakeAmmo
returns success without decrementing ammunition, while Revolverator's
controller still plays the native Walloper-strike animation.

The current test patch restores **only cached, owned vendor ammo metadata**
(fields +8, +10, +12, +14 and +18) that reverted exactly to their captured
native values, **only while the custom Revolverator projection is active**.
It does not refill ammo, modify the native TakeAmmo function, change melee
animations, or overwrite a conflicting third-party field. A regression test
simulates this same-level reload and verifies that live custom ammo remains
unchanged.

After installing the current test build:
1. Confirm Revolverator starts with a recognised capacity of 20.
2. Fire enough times to leave e.g. 13 rounds. Save a backup.
3. Die with Revolverator equipped; then respawn and attack once.
4. Check that the new log shows `max=20`, `consume_before=13`,
   `consume_after=12` (assuming one successful strike), and `custom=12`
   after commit.
5. Confirm the wheel remains independent. The currently preserved native
   Walloper icon may still appear for Revolverator after a respawn; that is
   a distinct custom-identity UI issue and must not be fixed by globally
   replacing the Walloper icon.

## How to get the test build

1. Open this fork's **Actions** tab. Newly forked repositories normally require
   manually enabling GitHub Actions (GitHub may show a warning banner).
2. If the test workflow did not run automatically, push another commit to
   `eqs-compat-test` *after* Actions have been enabled.
3. Open the workflow named **Revolverator EQS compatibility test (Windows)**.
   The workflow is branch-only, runs native tests, and uploads
   `revolverator-eqs-compat-windows-x86_64-TEST-ONLY` on success.
4. Download the artifact, extract the inner installable test ZIP. **Do not
   publish** this under the upstream project's release or the mod index.
5. This must **replace**, not coexist alongside, the official Revolverator
   build with the same mod ID/version. Keep a copy of the official package.

## Before an in-game test

- Back up your game save and **both mods' sidecar data**. If good wheel data
  was overwritten and saved, source changes will not magically restore it.
- Use the **Expanded Quick Select `compatibility-dev` build**, not its public
  v1.0.2 DLL: the release DLL lacks the optional exported query API.
- Keep the same four-wheel configuration used to reproduce the failure.
- Enable **Log Quick Select interoperability diagnostics** in the EQS
  development mod, and optionally `trace` in Revolverator.

## Test in stages

1. Before assigning a weapon, open the editor and switch pages 1–4. Inspect
   whether each page displays its own native assignments.
2. On **page 2**, replace **one ordinary/native weapon**. EQS's diagnostic log
   should show a change of **one or two** slots, rather than the previously
   reproduced **seven of eight** slots.
3. Check pages 3–4, close/reopen the editor, and confirm native assignments are
   not duplicated.
4. Open the gameplay wheel and verify native icons and actual equip behaviour.
   Check whether EQS still logs `Quick Select HUD layout mismatch`.
5. Check page 1 and Revolverator's normal weapon independently. Equip
   Revolverator, view pages 1–4, then switch to a native weapon and repeat.
   A native Walloper on a secondary page must REMAIN a Walloper when
   Revolverator is equipped. The screen should not spontaneously show a
   Revolverator icon on pages 2–4, and should not duplicate page one.
   After acquiring Revolverator, check that a gadget doesn't gain its icon.
   DO NOT remove or reassign a Walloper icon on a secondary page merely
   because a previous build displayed it as Revolverator.
6. Stop if the game crashes or assignments are unexpectedly cleared. Do not
   attempt to assign Revolverator itself to secondary pages in this iteration.

The expected success condition is **native wheel isolation and correct
native gameplay**; it is *not* full custom-weapon support. Log and screenshot
results are needed before deciding the follow-up change.
