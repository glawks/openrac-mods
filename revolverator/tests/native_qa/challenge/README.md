# Native PAL Challenge Mode regression

This Windows probe runs the actual PAL Challenge Mode transition on the
official OpenRAC runtime. It copies a generated fixture card into a new
profile, reads slot 0 with the native card driver and opens the game's
confirmation mode for starting Challenge Mode without writing a save.
It presses Cross to confirm and Start to skip Veldin's intro.

The stock and Revolverator runs use the same generated card. Before
confirmation, both place native item 10 and the Hoverboard in the same two
favourite slots and mark them owned. The candidate reads its Golden
upgrade, seven rounds, 117 XP and first favourite from the existing
arsenal sidecar. The probe leaves those values to the mod.

The witness observes the native new-game-plus hook, the incremented campaign
counter, the reset of the original equipment slots and the native removal of
the Hoverboard and its favourite. After 120 consecutive playable frames on
Veldin it checks the stock hand state and favourites, along with acquisition,
Golden status, XP, ammo and favourite preservation in the candidate.

## Running

Use a previously generated Golden QA PAL card with the expected values in
slot 0. The original fixture was `pal-save-03` from the PAL migration tests.
Its native file is SHA-256
`4aa7c64c6ae469b52de395f116f45f3f8f3db43256f8f90fd7ac6ca85d9144b3`,
and its arsenal sidecar is
`d155329cda48771a220bec4dbb00537ddc86762de2ff8ee50eeaad74f130f11c`.
The profile must contain test data only.

```powershell
cmake -S revolverator/tests/native_qa/challenge -B build-challenge "-DOpenRACMod_DIR=<sdk/cmake>"
cmake --build build-challenge --config Release
$fixture = @{
    Runtime = '<official PAL Windows executable>'
    Data = '<isolated source iso_data directory>'
    Levels = '<extracted PAL levels>'
    Probe = 'build-challenge/package/challenge_mode_probe'
    FixtureCard = '<generated PAL Golden QA cards directory>'
    Output = '<new stock results directory>'
}
& ./revolverator/tests/native_qa/challenge/run.ps1 @fixture
$fixture.Output = '<new candidate results directory>'
$fixture.Candidate = '<Revolverator ZIP or installed package directory>'
& ./revolverator/tests/native_qa/challenge/run.ps1 @fixture
```

Each run records the runtime, probe and fixture hashes, process exit and
probe checks in `result.json`. The runner also verifies that the copied
memory-card files remain byte-identical. The test covers PAL's real
transition and subsequent Veldin gameplay through the no-save route.
