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
