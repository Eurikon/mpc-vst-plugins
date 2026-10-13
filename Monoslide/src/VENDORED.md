# Vendored DSP

`dsp/SynthDSP.hpp` derives from `src/SynthDSP.hpp` in
[Eurikon/Twin-Slide](https://github.com/Eurikon/Twin-Slide), commit
`6f875764290b49a41e29c4fc7d6af17846e561a4`.
The source is vendored here; builds never fetch DSP or Rack assets.

## Local changes

- Removed the Rack dependency; supplied standard-library clamp and the original quadratic fine-tuning expression.
- Replaced Rack random noise with per-voice xorshift noise; instances share no RNG state.
- Removed unused AnalogEnvelope, oscillator helper methods, envelope helper methods, filter fields/constants and unused process timing argument/calculations.
- Extended voice reset to clear pitch slew, pre/post high-pass filters, accent decay, oscillator phase and envelope/gate flags.
- Removed decorative and paraphrasing comments; retained attribution. The Open303-derived synthesis path and fourfold oversampling remain.

`engine.cpp` is the new MPC adapter, not vendored Rack host code. It provides
MIDI note priority, velocity accents, atomic parameters, state serialization
and normalized stereo output. No TwinSlide sequencer or Rack UI is included.

## Licences

The TwinSlide-derived code is GPL-3.0-or-later; see root `LICENSE` and
`licenses/TwinSlide-LICENSE.txt`. Open303 is MIT, copyright Robin Schmidt;
see `licenses/Open303-MIT.txt`. Its Ooura FFT implementation is public domain.
Licence text line endings/trailing spaces were normalized without changing
words. No TwinSlide Rack panel or control artwork is distributed. The MPC skin
separately includes Twisted Cable's TC Link knob rotor and base artwork; the
rotor matches TC Link's asset, and the base differs only by removed comments.
