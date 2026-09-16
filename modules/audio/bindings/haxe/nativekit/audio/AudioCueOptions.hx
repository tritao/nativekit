package nativekit.audio;

import nativekit.audio.Enums.AudioCueOverflow;
import nativekit.audio.Enums.AudioCueSelection;

/** Immutable-at-use configuration copied by an AudioCue when it is created. */
class AudioCueOptions {
	/** Primitive source settings copied into every voice created by the cue. */
	public var voiceOptions:VoiceOptions;
	/** Maximum number of native voices retained by the cue; zero means unlimited. */
	public var maxVoices:Int = 8;
	/** Variant selection strategy used by subsequent play calls. */
	public var selection:AudioCueSelection = AudioCueSelection.Random;
	/** Local pool policy used when maxVoices is reached. */
	public var overflow:AudioCueOverflow = AudioCueOverflow.Drop;
	/** Inclusive lower bound for per-playback volume variation. */
	public var volumeMin:Float = 1.0;
	/** Inclusive upper bound for per-playback volume variation. */
	public var volumeMax:Float = 1.0;
	/** Inclusive lower bound for per-playback pitch variation. */
	public var pitchMin:Float = 1.0;
	/** Inclusive upper bound for per-playback pitch variation. */
	public var pitchMax:Float = 1.0;
	/** Default concurrency priority assigned to each new playback. */
	public var priority:Int = 0;

	public function new(?voiceOptions:VoiceOptions) {
		this.voiceOptions = copyVoiceOptions(voiceOptions);
	}

	static function copyVoiceOptions(source:VoiceOptions):VoiceOptions {
		var result = new VoiceOptions();
		if (source != null) {
			result.looping = source.looping;
			result.streaming = source.streaming;
			result.asynchronous = source.asynchronous;
		}
		return result;
	}
}
