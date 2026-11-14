# Changelog

All notable changes to NzDrum are listed here. Versions follow
[Semantic Versioning](https://semver.org); while the version starts with 0,
anything may still change between releases, including the saved session format.

## [0.2.0]

New interface, "Crema 80s": a cream hardware look designed in Figma.

- Redrawn pads, step keys coloured by beat, LED step levels, knobs with LCD
  value boxes and a sage LCD in the header.
- The header LCD shows the playing pattern, the next one, play or stop, tempo
  and the position as bar and beat.
- The pattern bank lights the playing pattern and blinks the queued one with a
  dashed outline.
- The sample view shows the start and end as red markers with their
  percentages.
- The window can be resized from 75 % to 200 %, keeping its proportions, and
  the size is saved with the session.
- Embedded fonts: Barlow Condensed and Share Tech Mono (SIL Open Font License).

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
