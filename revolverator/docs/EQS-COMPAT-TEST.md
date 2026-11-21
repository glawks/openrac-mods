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
   Each page should show its own native items regardless of the held weapon.
   After acquiring Revolverator, check that a gadget doesn't gain its icon
   unless the corresponding native wheel slot really uses carrier 18.
6. Stop if the game crashes or assignments are unexpectedly cleared. Do not
   attempt to assign Revolverator itself to secondary pages in this iteration.

The expected success condition is **native wheel isolation and correct
native gameplay**; it is *not* full custom-weapon support. Log and screenshot
results are needed before deciding the follow-up change.
