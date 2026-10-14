# Vendored DSP

`dsp/SynthDSP.hpp` derives from `src/SynthDSP.hpp` in [Eurikon/Twin-Slide](https://github.com/Eurikon/Twin-Slide), commit `6f875764290b49a41e29c4fc7d6af17846e561a4`.<br>
The source is vendored here; builds never fetch DSP or Rack assets.

## Local changes

- Removed the Rack dependency; supplied standard-library clamp and the original quadratic fine-tuning expression.
- Replaced Rack random noise with per-voice xorshift noise; instances share no RNG state.
- Removed unused AnalogEnvelope, oscillator helper methods, envelope helper methods, filter fields/constants and unused process timing argument/calculations.
- Extended voice reset to clear pitch slew, pre/post high-pass filters, accent decay, oscillator phase and envelope/gate flags.
- Cached exact DSP coefficients; they are recalculated when their inputs change, while audio processing, state updates and tails continue.
- Removed decorative and paraphrasing comments; retained attribution.<br>
  The Open303-derived synthesis path and fourfold oversampling remain.

`engine.cpp` is the new MPC adapter, not vendored Rack host code.<br>
It provides MIDI note priority, velocity accents, atomic parameters, state serialization and normalized stereo output.<br>
No TwinSlide sequencer or Rack UI is included.

## Licences

The TwinSlide-derived code is GPL-3.0-or-later; see root `LICENSE` and `licenses/TwinSlide-LICENSE.txt`.<br>
Open303 is MIT, copyright Robin Schmidt; see `licenses/Open303-MIT.txt`.<br>
Its Ooura FFT implementation is public domain.<br>
Licence text line endings/trailing spaces were normalized without changing words.<br>
No TwinSlide Rack panel or control artwork is distributed.<br>
The MPC skin separately includes Twisted Cable's TC Link knob rotor and base artwork; the rotor matches TC Link's asset, and the base differs only by removed comments.
