package nativekit.audio;

import NativeKit;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEvents.NativeKitEventSubscription;

/**
 * Associates an AudioCue with an entity-style spatial transform.
 *
 * The emitter borrows its cue and event pump. Transform setters update every
 * voice currently playing from this emitter; new voices inherit the latest
 * transform when play() is called.
 */
class AudioEmitter {
	final cue:AudioCue;
	final voices:Array<Voice> = [];
	final subscription:NativeKitEventSubscription;
	var position:Vector3 = Vector3.zero();
	var direction:Vector3 = new Vector3(0.0, 0.0, -1.0);
	var velocity:Vector3 = Vector3.zero();
	var disposed:Bool = false;

	public function new(cue:AudioCue, events:NativeKitEvents) {
		if (cue == null)
			throw "Audio emitter cue must not be null";
		if (events == null)
			throw "Audio emitter event pump must not be null";
		this.cue = cue;
		this.subscription = events.listen(onEvent);
	}

	/** Starts a cue voice with the current transform, or null when the cue is full. */
	public function play(?overrides:AudioPlayOptions):Null<Voice> {
		ensureLive();
		reapDisposedVoices();
		var voice = cue.play(overrides);
		if (voice == null)
			return null;
		try {
			applyTransform(voice, overrides);
			voices.push(voice);
			return voice;
		} catch (error:Dynamic) {
			cue.stop(voice);
			throw error;
		}
	}

	/** Updates the world or listener-relative position for all active voices. */
	public function setPosition(value:Vector3):Void {
		ensureLive();
		if (value == null)
			throw "Audio emitter position must not be null";
		position = value;
		forEachVoice(function(voice) voice.setPosition(position));
	}

	/** Updates the forward direction for all active voices. */
	public function setDirection(value:Vector3):Void {
		ensureLive();
		if (value == null)
			throw "Audio emitter direction must not be null";
		direction = value;
		forEachVoice(function(voice) voice.setDirection(direction));
	}

	/** Updates the world velocity for all active voices. */
	public function setVelocity(value:Vector3):Void {
		ensureLive();
		if (value == null)
			throw "Audio emitter velocity must not be null";
		velocity = value;
		forEachVoice(function(voice) voice.setVelocity(velocity));
	}

	/** Returns the latest emitter position. */
	public function getPosition():Vector3 {
		ensureLive();
		return position;
	}

	/** Returns the latest emitter direction. */
	public function getDirection():Vector3 {
		ensureLive();
		return direction;
	}

	/** Returns the latest emitter velocity. */
	public function getVelocity():Vector3 {
		ensureLive();
		return velocity;
	}

	/** Stops one voice owned by this emitter. */
	public function stop(voice:Voice):Bool {
		ensureLive();
		if (voice == null || !voices.remove(voice))
			return false;
		return cue.stop(voice);
	}

	/** Stops every voice owned by this emitter. */
	public function stopAll():Void {
		ensureLive();
		var failure:Dynamic = null;
		while (voices.length > 0) {
			var voice = voices.pop();
			try {
				cue.stop(voice);
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		if (failure != null)
			throw failure;
	}

	/** Returns the number of voices currently owned by this emitter. */
	public function activeVoiceCount():Int {
		ensureLive();
		reapDisposedVoices();
		return voices.length;
	}

	/** Releases the event subscription and stops this emitter's voices. */
	public function dispose():Void {
		if (disposed)
			return;
		subscription.dispose();
		var failure:Dynamic = null;
		while (voices.length > 0) {
			var voice = voices.pop();
			try {
				cue.stop(voice);
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		disposed = true;
		if (failure != null)
			throw failure;
	}

	public function isDisposed():Bool
		return disposed;

	function onEvent(value:NativeKitEventValue):Void {
		switch value {
			case AudioVoiceComplete(source): release(source);
			case AudioVoiceStolen(source): release(source);
			case AudioVoiceLoadFailed(source, _): release(source);
			case AudioVoiceStreamFailed(source, _): release(source);
			case _:
		}
	}

	function release(source:NativeKit.Handle):Void {
		for (index in 0...voices.length) {
			var voice = voices[index];
			if (!voice.isDisposed() && voice.nativeHandle().rawValue() == source.rawValue()) {
				voices.splice(index, 1);
				return;
			}
		}
	}

	function forEachVoice(action:Voice->Void):Void {
		reapDisposedVoices();
		for (voice in voices)
			action(voice);
	}

	function applyTransform(voice:Voice, overrides:Null<AudioPlayOptions>):Void {
		voice.setPosition(overrides != null && overrides.position != null ? overrides.position : position);
		voice.setDirection(overrides != null && overrides.direction != null ? overrides.direction : direction);
		voice.setVelocity(overrides != null && overrides.velocity != null ? overrides.velocity : velocity);
	}

	function reapDisposedVoices():Void {
		var index = voices.length - 1;
		while (index >= 0) {
			if (voices[index].isDisposed())
				voices.splice(index, 1);
			index--;
		}
	}

	function ensureLive():Void {
		if (disposed)
			throw "Audio emitter has been disposed";
	}
}
