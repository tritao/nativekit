package nativekit.audio;

/** Flags used when creating an audio voice. */
enum abstract VoiceLoadFlags(Int) from Int to Int {
	var Looping = 1;
	var Streaming = 2;
	var Asynchronous = 4;
}

/** Policy used when a bus must make room for a higher-priority voice. */
enum abstract VoiceStealPolicy(Int) from Int to Int {
	var None = 0;
	var Oldest = 1;
	var Quietest = 2;
	var LowestPriority = 3;
}

/** Chooses which clip variant an AudioCue starts for each playback. */
enum abstract AudioCueSelection(Int) from Int to Int {
	/** Chooses a variant using the Haxe runtime's random source. */
	var Random = 0;
	/** Visits variants in order and wraps back to the first variant. */
	var RoundRobin = 1;
}

/** Controls what an AudioCue does when its local voice pool is full. */
enum abstract AudioCueOverflow(Int) from Int to Int {
	/** Rejects the playback request and returns null. */
	var Drop = 0;
	/** Stops the oldest active cue voice and admits the new request. */
	var StealOldest = 1;
}
