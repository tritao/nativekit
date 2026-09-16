package nativekit.audio;

/** Playback settings copied into an AudioTrack descriptor. */
class AudioTrackOptions {
	/** Primitive source settings used when the track creates its voice. */
	public var voiceOptions:VoiceOptions;
	/** Linear gain target used by the track player. */
	public var volume:Float = 1.0;
	/** Pitch multiplier used by the track player. */
	public var pitch:Float = 1.0;
	/** Concurrency priority assigned to the track voice. */
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
