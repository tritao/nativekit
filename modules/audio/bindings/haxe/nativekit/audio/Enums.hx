package nativekit.audio;

/** Flags used when creating an audio voice. */
enum abstract VoiceLoadFlags(Int) from Int to Int {
	var Looping = 1;
	var Streaming = 2;
	var Asynchronous = 4;
}
