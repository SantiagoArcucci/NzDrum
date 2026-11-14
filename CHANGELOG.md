# Changelog

All notable changes to NzDrum are listed here. Versions follow
[Semantic Versioning](https://semver.org); while the version starts with 0,
anything may still change between releases, including the saved session format.

## [0.1.0]

First public release. macOS only, VST3 only.

- 16 pads in an MPC layout, played by click or MIDI (pad 1 = note 36, C1).
- Load your own samples (WAV, AIFF, FLAC, Ogg, MP3) by drag and drop or a file
  chooser, and step through the other files in the same folder with `<` and `>`.
- Step sequencer synced to the host: a bank of 16 patterns of 4, 8, 16 or 32
  steps, three levels per step (soft, normal, accent) and a semitone per step.
- Global swing from 50 % to 75 % on the even sixteenths.
- Pattern chaining through the automatable "Pattern" host parameter.
- Live recording with overdub, quantized to the nearest step.
- "16 Levels" mode: plays the selected pad chromatically across all 16 pads.
- Per pad: level, pan, pitch, decay, sample start and end (with a waveform
  view), mute and solo, all automatable.
- Copy, paste and clear for patterns; copy and paste for pads.
- Everything is saved with the DAW session, including each pad's sample path.
  Pads whose file is gone show "Missing".
