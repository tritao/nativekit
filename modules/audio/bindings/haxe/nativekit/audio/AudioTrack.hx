package nativekit.audio;

/**
 * Borrowed source descriptor for long-running playback.
 *
 * A track owns no native resources. Its clip must remain alive until every
 * player using the track has been disposed.
 */
class AudioTrack {
	public final clip:Clip;
	public final volume:Float;
	public final pitch:Float;
	public final priority:Int;
	final voiceOptions:VoiceOptions;

	public function new(clip:Clip, ?options:AudioTrackOptions) {
		if (clip == null)
			throw "Audio track clip must not be null";
		if (clip.isDisposed())
			throw "Audio track clip must be live";
		if (options == null)
			options = new AudioTrackOptions();
		if (!finite(options.volume) || options.volume < 0.0)
			throw "Audio track volume must be finite and non-negative";
		if (!finite(options.pitch) || options.pitch <= 0.0)
			throw "Audio track pitch must be finite and positive";
		if (options.priority < 0)
			throw "Audio track priority must be non-negative";
		this.clip = clip;
		this.volume = options.volume;
		this.pitch = options.pitch;
		this.priority = options.priority;
		this.voiceOptions = copyVoiceOptions(options.voiceOptions);
	}

	/** Returns whether this track creates looping voices. */
	public function isLooping():Bool
		return voiceOptions.looping;

	/** Creates a stopped voice configured for this track. */
	@:allow(nativekit.audio.AudioTrackPlayer)
	function createVoice():Voice
		return clip.createVoice(voiceOptions);

	static function copyVoiceOptions(source:VoiceOptions):VoiceOptions {
		var result = new VoiceOptions();
		if (source != null) {
			result.looping = source.looping;
			result.streaming = source.streaming;
			result.asynchronous = source.asynchronous;
		}
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
