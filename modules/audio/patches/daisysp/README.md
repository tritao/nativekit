# Private DaisySP safety adaptations

The upstream submodule remains unchanged. Translation wrappers in `src/daisysp`
select these small MIT implementations instead of their upstream `.cpp` files.
Their public classes and headers remain upstream-owned. Each adapted file
includes the upstream copyright and license; DaisySP-LICENSE is installed.

- `fm2.cpp`: initializes the carrier/modulator frequencies alongside their
  cache, so a 440 Hz note with ratio 2 does not retain the oscillators’ 100 Hz
  initialization default. Zero-index carrier tuning has a waveform regression.
  A separate two-oscillator reference checks 144 combinations of sample rate,
  carrier frequency, ratio, and index, each with pitch changes and retriggering.
  The upstream accumulating phase additions and index scaling are preserved.
- `granularplayer.cpp`: modulo sample wrapping handles exact-endpoint and
  multiple-wrap indices. Zero playback speed keeps phase instead of reversing it.
- `phasor.cpp`: wraps both the exact endpoint and increments spanning multiple
  cycles, keeping granular sample/envelope lookup phases inside [0,1).
- `drip.cpp`: feeds each resonator's computed local input into its state;
  upstream read uninitialized members. Normalizes random samples using RAND_MAX
  for portability. Rounds the attack countdown and expires it at/below zero.
- `KarplusString.cpp`: preserves negative nonlinearity values for the documented
  curved-bridge mode. Corrects delay compensation for the actual one-pole filter
  and DC blocker, including the internal rate used for low-pitch resampling. Reset
  consumes the initial excitation on the first call even below the delay-line
  range. Linear-mode pitch, natural decay, bridge selection, retriggers, and
  direct-model wrapper equivalence have regressions.
- `analogsnaredrum.cpp`: disables the Daisy SVF's nonlinear drive in the physical
  resonators and noise filter. Its original trigger excitation can make that
  nonlinear recurrence diverge at high pitches. Shell output still uses the
  model's soft clipping. Converts physical Q to the SVF's normalized resonance
  instead of saturating its control, and applies the trigger's one-pole filter
  instead of clamping it. The noise cutoff clamp is retained. A held-gate
  regression checks that the shell decays without an outer note release.

`audio_daisy_random.hpp` redirects vendor RNG calls into a scoped per-voice
xorshift state. It avoids global libc RNG locking and isolates simultaneous
engines/voices without heap allocations during processing. Random state resets
from MIDI pitch at note-on for reproducible patches. Header-based HiHat/Dust use
the same shim, scoped only around DaisySP includes. Models and envelopes are
reset on note-on; source configurations remain immutable for a patch's lifetime.

Keep these adaptations covered by the source lifecycle and control-boundary
regressions when updating the DaisySP pin.
