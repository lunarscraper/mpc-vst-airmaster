# mpc-vst-airmaster

Airwindows **Mastering** and **Mastering2** as VST2 **insert effects** for the Akai MPC OS plugin
host (Force, MPC Live/One/X/Key). Two plug-ins, one repo: `mastering/` and `mastering2/`.

The DSP is Airwindows' own source, unchanged (`Mastering*.cpp`, `Mastering2*.cpp` from
[airwindows/airwindows](https://github.com/airwindows/airwindows), `plugins/LinuxVST/src`),
MIT license, (c) Chris Johnson / Airwindows. So the sound is the original.

- **SIDEPASS** (Mastering2 only): elliptical EQ, cuts bass in the side channel (bass goes mono). Start 0.
- **GLUE**: tames harsh treble spikes. Start 0, meant to be used around the middle.
- **SCOPE**: fine detail / air. Start 50 = neutral.
- **SKRONK**: aggressive upper mids. Start 50.
- **GIRTH**: fatness of the low end. Start 50.
- **DRIVE**: trims the saturation of the bands, also moves the level a little. Start 50.
- **DITHER**: DARK, TEN NINES, TPDF WIDE, PAUL WIDE, NJAD, BYPASS (24 bit). Start BYPASS.

The plug-in shell (`airwin_shell.h`, the same file in both folders) is hand-written like
`mpc-vst-vfilter`'s, no wrapper; `audioeffectx.h` is a small stand-in for the Steinberg header so
the Airwindows sources compile as they are. Knobs pass their full position to Airwindows, the
0..100 on screen is only the readout.

**Presets**: 32 slots with LOAD/SAVE on the PRESET tab, in the pattern of `mpc-vst-rattler`: the
knob only browses, LOAD and SAVE act. The slots are also the plug-in's VST programs (host preset
list), shared by all projects and kept in `mastering_presets.txt` / `mastering2_presets.txt` on
the SD card next to the plug-in folder. Slot 1 is INIT (Airwindows' start values) until you
save over it; the other slots start empty. The current setting is stored with the project too.

`test.sh` runs the offline test (start values, level kept at the start setting, every control
fully up and down stays finite, chunk restore, preset SAVE/LOAD and the bank file) and a benchmark (about 2 % of an x86 core).

Build/deploy workflow: see `sd88me/mpc-vst-plugins`' `docs/PORTING.md`. The Actions workflow builds
both plug-ins (two jobs, tags `mastering-vst-v…` and `mastering2-vst-v…`).
License: MIT. No affiliation with Airwindows.

## Hinweis

Entwickelt mit Unterstützung von Claude (Anthropic)
