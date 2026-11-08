# NzDrum

*English · [Español](README.es.md)*

A 16-pad VST3 drum machine with a step sequencer and three dynamic levels per step (soft, normal and accent). Built in C++ with [JUCE](https://juce.com).

> A 16-pad sampler with a bank of 16 patterns of 4, 8, 16 or 32 steps, three levels and a semitone per step, global swing and live recording, synced to the host. Per pad: level, pan, pitch, decay, sample start and end, mute and solo. A chromatic "16 Levels" mode plays one sample across all 16 pads. Patterns are chained by automating the "Pattern" parameter in the DAW, and everything is saved with the session.

## How to use it

- **Load a sample:** drag an audio file (WAV, AIFF, FLAC, Ogg or MP3) onto a pad, or use the `...` button.
- **Browse the folder:** `<` and `>` step to the previous or next file in the same folder, in the same order as Finder. At the end it wraps back to the start.
- **Sequence:** clicking a pad selects it, and the row of 16 steps for that pad appears below. Click sets a step to normal, Shift-click to accent and Option-click to soft; repeating the same click turns it off. The mouse wheel over an active step changes its semitone (from −8 to +7, the same range as 16 Levels); the number shows on the step. The sequencer follows the DAW's transport and tempo.
- **Patterns:** the "Pattern" row picks one of the 16 patterns in the bank; patterns that have any step show a dot. If you switch patterns while playing, the switch waits until the current pattern ends (the one coming next blinks). With the DAW stopped, it switches right away.
- **Chain patterns (song mode):** the selected pattern is the host parameter **Pattern** (1 to 16). In Studio Pro, open the automation of the NzDrum track, choose "Pattern" and draw which pattern each section uses. You can also record it: with automation in write mode, clicks on the pattern buttons are recorded. A switch takes effect when the playing pattern ends, so it's best to place each change on the bar line or slightly before it. If it lands just after, it waits a whole pass of the pattern.
- **Length:** the "Steps" buttons set the selected pattern's length to 4, 8, 16 or 32 sixteenths. With 32, the steps are shown in two rows.
- **Copy, paste and clear patterns:** right-click (or Control-click) a pattern button. Copy takes a snapshot of the steps and the length; Clear turns off every step and resets the length to 16.
- **Live recording:** turn on `Rec` (at the left of the pattern bar) and press play in the DAW. Every pad hit, by click or MIDI, lands on the nearest step of the playing pattern and replaces what was there (overdub). Velocity picks the level; on the pads, Shift-click records an accent and Option-click a soft hit. With `16 Levels` on, the source pad is recorded with the semitone of the pad you hit.
- **Play a pad chromatically ("16 Levels"):** select a pad and turn on `16 Levels`, next to the knobs. All 16 pads play that sample, one semitone per pad: pad 9 at its original tuning, 1 to 8 lower and 10 to 16 higher. It works with clicks and MIDI; the sequencer keeps playing the regular pads. While it's on, hitting a pad doesn't change the selection.
- **Copy one pad to another:** right-click a pad. The audio and the settings (level, pan, pitch, decay, start and end) are copied, but not mute, solo or the steps.
- **Swing:** the control in the header applies to the whole bank. 50 % is straight and 75 % is the heaviest shuffle; it delays the even sixteenths (2, 4, 6...), like an MPC. It is a host parameter.
- **Edit the selected pad:** at the bottom is the sample's waveform, with two orange markers you can drag: where it starts and where it ends. They also move with the mouse wheel over the waveform: the wheel alone moves the start, with Shift the end, and with Option it moves in finer steps. Next to it are knobs for level, pan, pitch (±24 semitones) and decay (`off` = plays in full). Start and end are stored as a proportion of the file, so they stay put when you switch to another sample with `<` and `>`. Double-click a knob to reset it. You can type a value in each knob's text box, with or without a unit (`-6 dB`, `250 ms`, `30 L`).
- **Mute and solo:** each pad has `M` and `S` buttons. A muted pad never sounds: not from the sequencer, MIDI or a click. If any pads are soloed, only those play (unless they are also muted). The change is smoothed over 5 ms so it doesn't click.
- **Automation:** every per-pad setting (mute and solo included) is a host parameter, so it can be automated in Studio Pro.
- **Saving:** saving the Studio Pro session stores the 16 patterns with their lengths and semitones, the swing, the selected pattern, each pad's settings and the **path** to each pad's sample (the audio isn't copied into the session). If a file is gone when the session opens, the pad shows "Missing: name" in red and stays silent; `...`, `<` or `>` loads it again. The clipboard isn't saved.
- **Playing:** click a pad, or send MIDI notes. Pad 1 is note 36 (C1 in Studio Pro) and pad 16 is 51 (D#2). The grid follows the MPC layout: pad 1 at the bottom left.

## Requirements (macOS)

- macOS 11 or later (Apple Silicon or Intel)
- Xcode with the Command Line Tools (`xcode-select --install`)
- CMake 3.22 or later (`brew install cmake`)

You don't need to install JUCE: CMake downloads it the first time, pinned to version 9.0.5.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target NzDrum_VST3 --parallel
```

The first configure takes a while because it downloads JUCE. When the build finishes, the plugin is copied to `~/Library/Audio/Plug-Ins/VST3/NzDrum.vst3`. To skip the copy, configure with `-DNZDRUM_COPY_AFTER_BUILD=OFF`.

A standalone app is built too, handy for testing without opening a DAW:

```bash
cmake --build build --target NzDrum_Standalone --parallel
open build/NzDrum_artefacts/Release/Standalone/NzDrum.app
```

To debug in Xcode: `cmake -S . -B build-xcode -G Xcode` and open `build-xcode/NzDrum.xcodeproj`.

## Tests

The engine tests load real samples, play MIDI notes and check the audio that comes out, with no DAW needed:

```bash
cmake --build build --target NzDrumTests --parallel
./build/NzDrumTests_artefacts/Release/NzDrumTests
```

## Trying it in Fender Studio Pro

1. Build as above and restart Studio Pro (or rescan plugins).
2. Drag **NzDrum** onto an instrument track.
3. Load samples on a few pads and play them by clicking or with notes from C1.

## CI

Every push and pull request builds the universal VST3 on GitHub Actions, runs the engine tests, validates it with [pluginval](https://github.com/Tracktion/pluginval) and uploads it as a downloadable artifact. That binary isn't signed or notarized: to use it, macOS will ask you to allow it by hand (`xattr -dr com.apple.quarantine NzDrum.vst3`). For your own use, building it locally is simpler.

## License

The code in this repo is GPLv3 (see `LICENSE`). JUCE is used under the AGPLv3, so the compiled plugin is distributed under the terms of the AGPLv3.
