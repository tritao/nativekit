package nativekit.audio;

/** Optional per-playback overrides for an AudioCue or AudioEmitter. */
class AudioPlayOptions {
	/** Absolute gain override; null uses the cue's base and variation range. */
	public var volume:Null<Float> = null;
	/** Absolute pitch multiplier override; null uses the cue's variation range. */
	public var pitch:Null<Float> = null;
	/** Concurrency priority override; null uses the cue's configured priority. */
	public var priority:Null<Int> = null;
	/** Optional source position override. */
	public var position:Null<Vector3> = null;
	/** Optional source direction override. */
	public var direction:Null<Vector3> = null;
	/** Optional source velocity override. */
	public var velocity:Null<Vector3> = null;

	public function new() {}
}
