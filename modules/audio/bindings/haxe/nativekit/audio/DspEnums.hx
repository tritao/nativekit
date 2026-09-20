package nativekit.audio;

/** Built-in oscillator and LFO shapes. */
enum abstract DspWaveform(Int) from Int to Int {
	var Sine = 0;
	var Triangle = 1;
	var Saw = 2;
	var Square = 3;
}

/** Filter algorithms available to a DSP patch. */
enum abstract DspFilterType(Int) from Int to Int {
	var None = 0;
	var SvfLowPass = 1;
}

/** LFO phase behavior when a voice receives a note-on. */
enum abstract DspLfoMode(Int) from Int to Int {
	var Retrigger = 0;
	var FreeRunning = 1;
}

/** Sources available to a generic modulation route. */
enum abstract DspModulationSource(Int) from Int to Int {
	var Lfo = 0;
	var Envelope = 1;
}

/** Destinations available to a generic modulation route. */
enum abstract DspModulationDestination(Int) from Int to Int {
	var PitchSemitones = 0;
	var FilterCutoffHz = 1;
	var Amplitude = 2;
	var OscillatorLevel = 3;
	var OscillatorPhase = 4;
}

/** Normalizes a modulation source before its route amount is applied. */
enum abstract DspModulationPolarity(Int) from Int to Int {
	var Bipolar = 0;
	var Unipolar = 1;
}

/** Instrument parameters accepted by the native DSP instrument. */
enum abstract DspParameter(Int) from Int to Int {
	var Waveform = 0;
	var Gain = 1;
	var AttackSeconds = 2;
	var DecaySeconds = 3;
	var SustainLevel = 4;
	var ReleaseSeconds = 5;
	var NoiseLevel = 6;
	var FilterCutoffHz = 7;
	var FilterResonance = 8;
	var OscillatorWaveform = 9;
	var OscillatorLevel = 10;
	var OscillatorDetuneCents = 11;
	var OscillatorPhase = 12;
}
