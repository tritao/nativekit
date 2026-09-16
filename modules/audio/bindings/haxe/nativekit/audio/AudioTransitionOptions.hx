package nativekit.audio;

/** Shared-clock timing settings for AudioTrackPlayer transitions. */
class AudioTransitionOptions {
	/** Seconds for the incoming track to reach its configured volume. */
	public var fadeInSeconds:Float = 0.0;
	/** Seconds for the outgoing track to reach silence. */
	public var fadeOutSeconds:Float = 0.0;
	/** Seconds to wait before starting the transition. */
	public var delaySeconds:Float = 0.0;

	public function new() {}
}
