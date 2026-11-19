# Gaspar helmet pickup regression

This test uses an empty memory card profile and the real Pilot's Helmet already
placed on Gaspar. The probe positions Ratchet beside the pickup and lets the
native proximity check, cutscene and acquisition run. It checks that the grant
assigns the helmet to the head slot, preserves the hand slot and lets gameplay
continue for at least 120 playable frames.

From the repository root, configure and build the standalone probe with the
OpenRAC Mod SDK and Visual Studio, then run it from PowerShell 7:

```powershell
cmake -S revolverator/tests/native_qa/gaspar -B build-gaspar-probe "-DOpenRACMod_DIR=<sdk/cmake>"
cmake --build build-gaspar-probe --config Release
& ./revolverator/tests/native_qa/gaspar/run.ps1 `
  -Runtime '<official Windows PAL executable>' `
  -Data '<local iso_data directory>' -Levels '<extracted PAL levels>' `
  -Probe build-gaspar-probe/package/gaspar_helmet_probe `
  -Candidate '<installable mod ZIP or package folder>' -Output '<new output folder>'
```

Omit `-Candidate` to establish the baseline without Revolverator. With a
candidate, the script enables its starting unlock so the pickup is tested while
the weapon is equipped. The output records exit status, exact DLL hashes and
the observed grant and equipment state. The published 0.3.3 comparison is
expected to fail. See the [Gaspar receipt](../../../docs/validation/gaspar-helmet-0.3.4.json).
