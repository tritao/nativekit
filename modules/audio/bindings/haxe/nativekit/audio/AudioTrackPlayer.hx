package nativekit.audio;

import NativeKit;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEvents.NativeKitEventSubscription;

/**
 * Generic coordinator for one long-running audio track and one queued track.
 *
 * The player borrows tracks, the optional bus, and the event pump. Call
 * update() once per frame after polling NativeKitRuntime.events so scheduled
 * transition and stop voices can be retired on the Haxe side.
 */
class AudioTrackPlayer {
	final events:NativeKitEvents;
	final bus:Null<Bus>;
	final subscription:NativeKitEventSubscription;
	var currentTrackValue:Null<AudioTrack>;
	var currentVoiceValue:Null<Voice>;
	var outgoingTrackValue:Null<AudioTrack>;
	var outgoingVoiceValue:Null<Voice>;
	var transitionStartFrames:Null<haxe.Int64>;
	var transitionEndFrames:Null<haxe.Int64>;
	var transitionActive:Bool = false;
	var stopEndFrames:Null<haxe.Int64>;
	var queuedTrackValue:Null<AudioTrack>;
	var queuedOptionsValue:Null<AudioTransitionOptions>;
	var disposed:Bool = false;

	/** Called when a voice has been admitted and its start command submitted. */
	public var onTrackStarted:AudioTrack->Void = null;
	/** Called when a track leaves the player, naturally or through replacement. */
	public var onTrackEnded:AudioTrack->Void = null;
	/** Called when a track's asynchronous source fails. */
	public var onTrackFailed:AudioTrack->NativeKit.Result->Void = null;
	/** Called after the outgoing and incoming tracks have been scheduled. */
	public var onTransitionStarted:AudioTrack->AudioTrack->Void = null;
	/** Called when a crossfade reaches its shared-clock end. */
	public var onTransitionCompleted:AudioTrack->Void = null;
	/** Called when a pending crossfade is superseded before completion. */
	public var onTransitionCancelled:AudioTrack->Void = null;

	public function new(events:NativeKitEvents, ?bus:Bus) {
		if (events == null)
			throw "Audio track player event pump must not be null";
		if (bus != null && bus.isDisposed())
			throw "Audio track player bus must be live";
		this.events = events;
		this.bus = bus;
		this.subscription = events.listen(onNativeEvent);
	}

	/** Starts a track, replacing the current track; supplied timing enables a transition-style start. */
	public function play(track:AudioTrack, ?options:AudioTransitionOptions):Voice {
		ensureLive();
		if (track == null)
			throw "Audio track player track must not be null";
		var timing = copyOptions(options);
		validateOptions(timing);
		if (options != null && currentVoiceValue != null)
			return transitionTo(track, timing);
		settleTransitionForReplacement();
		clearQueued();
		stopCurrent(true);
		return startCurrent(track, timing);
	}

	/**
	 * Crossfades to a track using equal fade-in and fade-out durations.
	 *
	 * The transition starts immediately unless the duration is zero. The
	 * duration is expressed in seconds and converted to the engine's PCM clock.
	 */
	public function crossfadeTo(track:AudioTrack, durationSeconds:Float = 1.0):Voice {
		var options = new AudioTransitionOptions();
		options.fadeInSeconds = durationSeconds;
		options.fadeOutSeconds = durationSeconds;
		return transitionTo(track, options);
	}

	/** Crossfades to a track with independent fade and delay settings. */
	public function transitionTo(track:AudioTrack, ?options:AudioTransitionOptions):Voice {
		ensureLive();
		if (track == null)
			throw "Audio track player track must not be null";
		var timing = copyOptions(options);
		validateOptions(timing);
		update();
		if (currentVoiceValue == null)
			return startCurrent(track, timing);
		settleTransitionForReplacement();
		if (currentVoiceValue == null)
			return startCurrent(track, timing);

		var oldTrack = currentTrackValue;
		var oldVoice = currentVoiceValue;
		if (stopEndFrames != null) {
			oldVoice.clearSchedule();
			stopEndFrames = null;
		}
		var now = Mixer.timeFrames();
		var start = haxe.Int64.add(now, secondsToFrames(timing.delaySeconds));
		var fadeInFrames = secondsToFrames(timing.fadeInSeconds);
		var fadeOutFrames = secondsToFrames(timing.fadeOutSeconds);
		var transitionFrames = fadeInFrames > fadeOutFrames ? fadeInFrames : fadeOutFrames;
		var incoming = createScheduledVoice(track, start, fadeInFrames);
		try {
			if (fadeOutFrames == 0)
				oldVoice.scheduleStop(start);
			else {
				oldVoice.fadeAt(Voice.CURRENT_VOLUME, 0.0, fadeOutFrames, start);
				oldVoice.scheduleStop(haxe.Int64.add(start, fadeOutFrames));
			}
		} catch (error:Dynamic) {
			stopAndDispose(incoming);
			throw error;
		}

		outgoingTrackValue = oldTrack;
		outgoingVoiceValue = oldVoice;
		currentTrackValue = track;
		currentVoiceValue = incoming;
		transitionStartFrames = start;
		transitionEndFrames = haxe.Int64.add(start, transitionFrames);
		transitionActive = true;
		if (onTransitionStarted != null && oldTrack != null)
			onTransitionStarted(oldTrack, track);
		emitTrackStarted(track);
		return incoming;
	}

	/** Queues one track to start after the current non-looping track ends. */
	public function queue(track:AudioTrack, ?options:AudioTransitionOptions):Void {
		ensureLive();
		if (track == null)
			throw "Audio track player queued track must not be null";
		var timing = copyOptions(options);
		validateOptions(timing);
		if (currentVoiceValue == null) {
			startCurrent(track, timing);
			return;
		}
		queuedTrackValue = track;
		queuedOptionsValue = timing;
	}

	/** Stops playback immediately or after a fade-out. */
	public function stop(fadeOutSeconds:Float = 0.0):Void {
		ensureLive();
		if (!finite(fadeOutSeconds) || fadeOutSeconds < 0.0)
			throw "Audio track player fade-out must be finite and non-negative";
		update();
		settleTransitionForReplacement();
		clearQueued();
		if (currentVoiceValue == null)
			return;
		if (fadeOutSeconds == 0.0) {
			stopCurrent(true);
			return;
		}
		var now = Mixer.timeFrames();
		var frames = secondsToFrames(fadeOutSeconds);
		if (haxe.Int64.compare(frames, haxe.Int64.ofInt(0)) == 0) {
			stopCurrent(true);
			return;
		}
		currentVoiceValue.clearSchedule();
		currentVoiceValue.fadeAt(Voice.CURRENT_VOLUME, 0.0, frames, now);
		stopEndFrames = haxe.Int64.add(now, frames);
		currentVoiceValue.scheduleStop(stopEndFrames);
	}

	/** Completes scheduled transition and stop cleanup for the current frame. */
	public function update():Void {
		ensureLive();
		var now = Mixer.timeFrames();
		if (stopEndFrames != null && haxe.Int64.compare(now, stopEndFrames) >= 0)
			finishStop();
		if (transitionActive && transitionEndFrames != null &&
			haxe.Int64.compare(now, transitionEndFrames) >= 0)
			finishTransition();
	}

	/** Returns the track currently selected as the player destination. */
	public function currentTrack():Null<AudioTrack> {
		ensureLive();
		update();
		return currentTrackValue;
	}

	/** Returns the current native voice, or null when stopped. */
	public function currentVoice():Null<Voice> {
		ensureLive();
		update();
		return currentVoiceValue;
	}

	/** Returns the queued track waiting for a natural end, or null. */
	public function queuedTrack():Null<AudioTrack> {
		ensureLive();
		return queuedTrackValue;
	}

	public function isTransitioning():Bool {
		ensureLive();
		update();
		return transitionActive;
	}

	public function isStopping():Bool {
		ensureLive();
		update();
		return stopEndFrames != null;
	}

	/** Releases all player voices and the event subscription. */
	public function dispose():Void {
		if (disposed)
			return;
		subscription.dispose();
		if (outgoingVoiceValue != null)
			stopAndDispose(outgoingVoiceValue);
		if (currentVoiceValue != null)
			stopAndDispose(currentVoiceValue);
		outgoingVoiceValue = null;
		currentVoiceValue = null;
		outgoingTrackValue = null;
		currentTrackValue = null;
		transitionStartFrames = null;
		transitionEndFrames = null;
		stopEndFrames = null;
		clearQueued();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function onNativeEvent(value:NativeKitEventValue):Void {
		switch value {
			case AudioVoiceComplete(source): handleVoiceEnded(source);
			case AudioVoiceStolen(source): handleVoiceEnded(source);
			case AudioVoiceLoadFailed(source, result): handleVoiceFailed(source, result);
			case AudioVoiceStreamFailed(source, result): handleVoiceFailed(source, result);
			case _: return;
		}
	}

	function handleVoiceEnded(source:NativeKit.Handle):Void {
		if (matches(source, outgoingVoiceValue)) {
			var ended = outgoingTrackValue;
			var voice = outgoingVoiceValue;
			outgoingTrackValue = null;
			outgoingVoiceValue = null;
			stopAndDispose(voice);
			if (ended != null)
				emitTrackEnded(ended);
			return;
		}
		if (!matches(source, currentVoiceValue))
			return;
		var ended = currentTrackValue;
		var voice = currentVoiceValue;
		currentTrackValue = null;
		currentVoiceValue = null;
		stopEndFrames = null;
		if (transitionActive) {
			cancelTransitionVoices();
		}
		stopAndDispose(voice);
		if (ended != null)
			emitTrackEnded(ended);
		startQueued();
	}

	function handleVoiceFailed(source:NativeKit.Handle, result:NativeKit.Result):Void {
		if (matches(source, outgoingVoiceValue)) {
			var failed = outgoingTrackValue;
			var voice = outgoingVoiceValue;
			outgoingTrackValue = null;
			outgoingVoiceValue = null;
			stopAndDispose(voice);
			if (failed != null) {
				if (onTrackFailed != null)
					onTrackFailed(failed, result);
				emitTrackEnded(failed);
			}
			return;
		}
		if (!matches(source, currentVoiceValue))
			return;
		var failed = currentTrackValue;
		var voice = currentVoiceValue;
		currentTrackValue = null;
		currentVoiceValue = null;
		stopEndFrames = null;
		if (transitionActive)
			cancelTransitionVoices();
		stopAndDispose(voice);
		if (failed != null) {
			if (onTrackFailed != null)
				onTrackFailed(failed, result);
			emitTrackEnded(failed);
		}
		startQueued();
	}

	function startCurrent(track:AudioTrack, options:AudioTransitionOptions):Voice {
		var now = Mixer.timeFrames();
		var start = haxe.Int64.add(now, secondsToFrames(options.delaySeconds));
		var voice = createScheduledVoice(track, start,
			secondsToFrames(options.fadeInSeconds));
		currentTrackValue = track;
		currentVoiceValue = voice;
		stopEndFrames = null;
		emitTrackStarted(track);
		return voice;
	}

	function createScheduledVoice(track:AudioTrack, start:haxe.Int64, fadeInFrames:haxe.Int64):Voice {
		var voice = track.createVoice();
		try {
			voice.setBus(bus);
			voice.setPriority(track.priority);
			voice.setPitch(track.pitch);
			if (haxe.Int64.compare(start, Mixer.timeFrames()) > 0)
				voice.scheduleStart(start);
			if (haxe.Int64.compare(fadeInFrames, haxe.Int64.ofInt(0)) == 0)
				voice.setVolume(track.volume);
			else {
				voice.setVolume(0.0);
				voice.fadeAt(0.0, track.volume, fadeInFrames, start);
			}
			voice.start();
			return voice;
		} catch (error:Dynamic) {
			stopAndDispose(voice);
			throw error;
		}
	}

	function settleTransitionForReplacement():Void {
		if (!transitionActive)
			return;
		var oldTrack = outgoingTrackValue;
		var oldVoice = outgoingVoiceValue;
		var incoming = currentVoiceValue;
		var incomingTrack = currentTrackValue;
		if (oldVoice != null) {
			oldVoice.clearSchedule();
			stopAndDispose(oldVoice);
		}
		if (incoming != null) {
			incoming.clearSchedule();
			if (!incoming.isPlaying()) {
				incoming.setVolume(incomingTrack == null ? 1.0 : incomingTrack.volume);
				incoming.start();
			}
		}
		outgoingTrackValue = null;
		outgoingVoiceValue = null;
		transitionStartFrames = null;
		transitionEndFrames = null;
		transitionActive = false;
		if (oldTrack != null)
			emitTrackEnded(oldTrack);
		if (incomingTrack != null && onTransitionCancelled != null)
			onTransitionCancelled(incomingTrack);
	}

	function finishTransition():Void {
		if (!transitionActive)
			return;
		var oldTrack = outgoingTrackValue;
		var oldVoice = outgoingVoiceValue;
		var current = currentTrackValue;
		outgoingTrackValue = null;
		outgoingVoiceValue = null;
		transitionStartFrames = null;
		transitionEndFrames = null;
		transitionActive = false;
		if (oldVoice != null)
			stopAndDispose(oldVoice);
		if (oldTrack != null)
			emitTrackEnded(oldTrack);
		if (current != null && onTransitionCompleted != null)
			onTransitionCompleted(current);
	}

	function finishStop():Void {
		var ended = currentTrackValue;
		var voice = currentVoiceValue;
		currentTrackValue = null;
		currentVoiceValue = null;
		stopEndFrames = null;
		if (voice != null)
			stopAndDispose(voice);
		if (ended != null)
			emitTrackEnded(ended);
	}

	function stopCurrent(emitEnded:Bool):Void {
		var ended = currentTrackValue;
		var voice = currentVoiceValue;
		currentTrackValue = null;
		currentVoiceValue = null;
		stopEndFrames = null;
		if (voice != null)
			stopAndDispose(voice);
		if (emitEnded && ended != null)
			emitTrackEnded(ended);
	}

	function cancelTransitionVoices():Void {
		var oldTrack = outgoingTrackValue;
		var oldVoice = outgoingVoiceValue;
		outgoingTrackValue = null;
		outgoingVoiceValue = null;
		transitionStartFrames = null;
		transitionEndFrames = null;
		transitionActive = false;
		if (oldVoice != null)
			stopAndDispose(oldVoice);
		if (oldTrack != null)
			emitTrackEnded(oldTrack);
	}

	function startQueued():Void {
		if (queuedTrackValue == null)
			return;
		var track = queuedTrackValue;
		var options = queuedOptionsValue;
		clearQueued();
		if (track != null)
			startCurrent(track, options == null ? new AudioTransitionOptions() : options);
	}

	function clearQueued():Void {
		queuedTrackValue = null;
		queuedOptionsValue = null;
	}

	function emitTrackStarted(track:AudioTrack):Void {
		if (onTrackStarted != null)
			onTrackStarted(track);
	}

	function emitTrackEnded(track:AudioTrack):Void {
		if (onTrackEnded != null)
			onTrackEnded(track);
	}

	function matches(source:NativeKit.Handle, voice:Null<Voice>):Bool {
		return voice != null && !voice.isDisposed() &&
			voice.nativeHandle().rawValue() == source.rawValue();
	}

	static function copyOptions(source:Null<AudioTransitionOptions>):AudioTransitionOptions {
		var result = new AudioTransitionOptions();
		if (source != null) {
			result.fadeInSeconds = source.fadeInSeconds;
			result.fadeOutSeconds = source.fadeOutSeconds;
			result.delaySeconds = source.delaySeconds;
		}
		return result;
	}

	static function validateOptions(options:AudioTransitionOptions):Void {
		if (!finite(options.fadeInSeconds) || options.fadeInSeconds < 0.0 ||
			!finite(options.fadeOutSeconds) || options.fadeOutSeconds < 0.0 ||
			!finite(options.delaySeconds) || options.delaySeconds < 0.0)
			throw "Audio transition timing must be finite and non-negative";
	}

	static function secondsToFrames(seconds:Float):haxe.Int64 {
		return haxe.Int64.fromFloat(seconds * Mixer.sampleRate());
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	function stopAndDispose(voice:Voice):Void {
		if (voice == null || voice.isDisposed())
			return;
		try {
			voice.clearSchedule();
		} catch (_:Dynamic) {}
		try {
			voice.stop();
		} catch (_:Dynamic) {}
		voice.dispose();
	}

	function ensureLive():Void {
		if (disposed)
			throw "Audio track player has been disposed";
	}
}
