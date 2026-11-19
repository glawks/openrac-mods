# Release verification checklist

This is a verification plan, not a claim that every box has passed. The release
receipt records completed observations against exact artifacts. Historical
results are indexed in [NATIVE_VALIDATION.md](NATIVE_VALIDATION.md).

## Installation and compatibility

- Install the public ZIP through the official launcher with only Revolverator
  enabled, then test the optional XP combination separately.
- Confirm the minimum interface and required capabilities; supported later
  clients must not be rejected solely by version number.
- Start with an empty local asset cache. Confirm automatic derivation, normal
  gameplay startup and unchanged native files; repeat with a warm cache.
- Confirm no copied game art, patched client, separate Python dependency or
  manual utility-card step is required.
- Check notices, version, source archive, hashes and public downloads.

## Contact sparks and presentation

- Observe native sparks at the actual contact during a captured spin.
- Confirm rate follows speed, remains bounded and does not accrue in pause.
- Confirm no contact emission at idle, on a miss, during equip construction,
  while empty, after release, after target loss or during menu previews.
- Exercise target replacement, cancellation, death, equipment changes and
  level transitions without stale entity or particle pointers.
- Keep Base and Golden, native Walloper, attack enlargement, normal carrying
  geometry, acquisition scenes, menus and the cartridge HUD distinct.

## Gameplay and persistence

- Collect the placed Pilot's Helmet on Gaspar with Revolverator enabled.
  Confirm helmet ownership and native head equipment after the pickup,
  preserved weapon selection, and continued playable frames.
- Cover regular weapons, gadgets and wearable equipment at the grant
  boundary with auto-equip enabled and disabled. Check native quick-select
  updates and ensure only Revolverator can request its own manual hand switch.
- Test tap versus minimum uninterrupted hold, capture, spin, throw, impact and
  rapid reuse. Verify one round per activation and capacity 20.
- Test Hoven purchase, normal ammo pickup/refill and the Novalis Golden purchase.
- Open Gadgetron before Revolverator appears, then open a list with a supported
  Revolverator weapon or ammo row. Check that the stock-only list uses the
  original draw and both lists keep native scroll and selection behavior.
- During acquisition, queue a native head or body change and a newer hand
  selection. Completion must leave those updates in place.
- Verify excluded heavy enemies receive native melee damage; Blarg Commanders
  remain capturable. Check eligible light targets near excluded targets.
- Confirm the 50% carry-speed limit and release recovery without altering
  vertical motion, platform movement or camera control.
- Exercise weapon-menu confirmation, scrolling and eight quick-select slots.
- Save and reload independent ownership/ammo/Gold/favorites; reject corrupt,
  absent or mismatched sidecars and preserve native Walloper state.
- Run New Game Plus on an isolated card. Check that the native reset determines
  which stock favorites and equipment survive, and that owned Revolverator
  bindings return without replacing a native favorite.
- With XP enabled, confirm earned custom XP, separate Walloper XP, level-up
  presentation and no credit for suppressed hits or duplicate death records.

## Charge release in 0.3.4

- Release one displayed frame before full: the live enemy drops onto the floor
  in front of Ratchet, returns to native behavior and can counterattack.
- Confirm an early drop creates no projectile, thrown impact, Golden blast or
  throw animation, and does not refund the activation's ammunition.
- Release with the bar already full at 1.6 seconds of spinning: normal launch
  and Golden impact behavior remain available. Hold to the three-second maximum
  and confirm the existing automatic launch.
- Repeat Base and Golden early/full cases, rapid re-use, pause around the charge
  boundary, invalid targets, safety cancellation and an independent older flight.
- Check enemy collision, floor height, orientation and live native updates after
  a drop. Do not count a host-only World::drop call as native placement proof.

## Native sounds in the current 0.3.4 runtime

- Confirm the production target does not link the custom sound adapter or
  connect a custom sound sink. The original native equip path must remain.
- Exercise Base/Golden charge, drop, throw, pause and purchase with the exact
  candidate DLL. Confirm no custom sound load, play or stop events.
- Verify the installable archive contains no assets/audio files, including
  stale files from a previous build. Preserve the parked experiment only in
  the corresponding source snapshot.

## Historical custom sound experiment

The following checklist applies only if the experiment is intentionally resumed;
it is not a requirement or description of the current native-only runtime.

- Verify all 14 shipped WAV hashes, source/license closure, PCM headers and catalog paths
  against the frozen handoff. No copied game audio may enter the package.
- Confirm custom ammo_pickup, dry_fire, equip and xp_level_up are neither loaded nor triggered;
  their normal game sounds remain. Keep the audition-only acceleration_demo
  out of both playback and distributed runtime files.
- Observe actual cue edges and active voices for capture, held spin, charged
  release, early drop, actual landing, full charge, ammunition and acquisition.
- Confirm early drop does not also play throw, projectile impact or explosion
  for the same actor; charge-ready triggers once per crossing.
- Stop motor voices during pause, empty ammunition, weapon changes, target loss,
  death, level change and mod teardown. Resume without event backlogs or doubled
  loop voices. Keep audition-only acceleration_demo out of normal playback.
- Record native cue/voice evidence separately from a real listening/mix review.
  File hashes, host tests and muted native fixtures do not establish audible
  quality or user approval.

## Evidence boundary

Use isolated cards/settings for automated fixtures and record seeded setup.
Observe exit codes, original-file hashes and renderer output. Host suites and
regional bindings do not establish full-campaign, every-boss, every-language
or arbitrary mod-combination compatibility.
