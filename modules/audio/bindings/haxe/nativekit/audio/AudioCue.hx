package nativekit.audio;

import NativeKit;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEvents.NativeKitEventSubscription;
import nativekit.audio.Enums.AudioCueOverflow;
import nativekit.audio.Enums.AudioCueSelection;

/**
 * A reusable gameplay sound backed by a small pool of NativeKit voices.
 *
 * A cue may contain several clip variants. It borrows every clip, its bus,
 * and the event pump. The caller must keep those objects alive while the cue
 * is in use and must poll the event pump for completed voices to become
 * reusable.
 */
class AudioCue {
	final variants:Array<Clip>;
	final events:NativeKitEvents;
	final bus:Null<Bus>;
	final voiceOptions:VoiceOptions;
	final maxVoices:Int;
	final voices:Array<Voice> = [];
	final voiceVariants:Array<Int> = [];
	final available:Array<Voice> = [];
	final active:Array<Voice> = [];
	final subscription:NativeKitEventSubscription;
	var nextVariant:Int = 0;
	var disposed:Bool = false;

	/** Linear gain multiplied by the configured per-playback volume range. */
	public var volume:Float = 1.0;
	/** Pitch multiplier multiplied by the configured per-playback pitch range. */
	public var pitch:Float = 1.0;
	/** Priority used by the native bus concurrency policy. */
	public var priority:Int;

	/** Variant selection strategy for future play calls. */
	public var selection:AudioCueSelection;
	/** Pool overflow policy for future play calls. */
	public var overflow:AudioCueOverflow;
	/** Inclusive lower volume variation bound. */
	public var volumeMin:Float;
	/** Inclusive upper volume variation bound. */
	public var volumeMax:Float;
	/** Inclusive lower pitch variation bound. */
	public var pitchMin:Float;
	/** Inclusive upper pitch variation bound. */
	public var pitchMax:Float;

	/** Creates a cue containing one clip. */
	public static function fromClip(clip:Clip, events:NativeKitEvents, ?bus:Bus,
		?voiceOptions:VoiceOptions, maxVoices:Int = 8):AudioCue {
		var options = new AudioCueOptions(voiceOptions);
		options.maxVoices = maxVoices;
		return new AudioCue([clip], events, bus, options);
	}

	/** Creates a cue containing one or more clip variants. */
	public static function fromClips(clips:Array<Clip>, events:NativeKitEvents, ?bus:Bus,
		?options:AudioCueOptions):AudioCue {
		return new AudioCue(clips, events, bus, options);
	}

	private function new(clips:Array<Clip>, events:NativeKitEvents, bus:Null<Bus>,
		options:Null<AudioCueOptions>) {
		if (clips == null || clips.length == 0)
			throw "Audio cue must contain at least one clip";
		if (events == null)
			throw "Audio cue event pump must not be null";
		for (clip in clips) {
			if (clip == null)
				throw "Audio cue clips must not contain null values";
			if (clip.isDisposed())
				throw "Audio cue clips must be live";
		}
		if (options == null)
			options = new AudioCueOptions();
		validateOptions(options);
		this.variants = clips.copy();
		this.events = events;
		this.bus = bus;
		this.voiceOptions = copyVoiceOptions(options.voiceOptions);
		this.maxVoices = options.maxVoices;
		this.selection = options.selection;
		this.overflow = options.overflow;
		this.volumeMin = options.volumeMin;
		this.volumeMax = options.volumeMax;
		this.pitchMin = options.pitchMin;
		this.pitchMax = options.pitchMax;
		this.priority = options.priority;
		this.subscription = events.listen(onEvent);
	}

	/** Returns the number of clip variants in this cue. */
	public function variantCount():Int {
		ensureLive();
		return variants.length;
	}

	/** Returns a borrowed clip variant by index. */
	public function variant(index:Int):Clip {
		ensureLive();
		if (index < 0 || index >= variants.length)
			throw "Audio cue variant index is out of range";
		return variants[index];
	}

	/** Returns the variant index used to create a pooled voice, or -1 if unknown. */
	public function variantIndex(voice:Voice):Int {
		ensureLive();
		if (voice == null)
			return -1;
		var index = voices.indexOf(voice);
		return index < 0 ? -1 : voiceVariants[index];
	}

	/** Starts a pooled voice, or returns null when the configured pool drops it. */
	public function play(?overrides:AudioPlayOptions):Null<Voice> {
		ensureLive();
		reapDisposedVoices();
		var selectedVariant = chooseVariant();
		var effectiveVolume = overrides != null && overrides.volume != null
			? overrides.volume
			: volume * randomRange(volumeMin, volumeMax);
		var effectivePitch = overrides != null && overrides.pitch != null
			? overrides.pitch
			: pitch * randomRange(pitchMin, pitchMax);
		var effectivePriority = overrides != null && overrides.priority != null
			? overrides.priority
			: priority;
		validatePlayback(effectiveVolume, effectivePitch, effectivePriority);

		var voice:Voice = takeAvailable(selectedVariant);
		var reused = voice != null;
		if (voice == null && maxVoices > 0 && voices.length >= maxVoices) {
			if (overflow == AudioCueOverflow.Drop)
				return null;
			voice = stealOldest(selectedVariant);
			reused = voice != null;
			if (voice == null && maxVoices > 0 && voices.length >= maxVoices)
				return null;
		}
		if (voice == null) {
			voice = variants[selectedVariant].createVoice(voiceOptions);
			voices.push(voice);
			voiceVariants.push(selectedVariant);
		}

		try {
			voice.setBus(bus);
			voice.setPriority(effectivePriority);
			voice.setVolume(effectiveVolume);
			voice.setPitch(effectivePitch);
			if (overrides != null) {
				if (overrides.position != null)
					voice.setPosition(overrides.position);
				if (overrides.direction != null)
					voice.setDirection(overrides.direction);
				if (overrides.velocity != null)
					voice.setVelocity(overrides.velocity);
			}
			if (reused)
				voice.rewind();
			voice.start();
			active.push(voice);
			return voice;
		} catch (error:Dynamic) {
			if (!voice.isDisposed() && !available.contains(voice))
				available.push(voice);
			throw error;
		}
	}

	/** Stops every active cue voice and makes it available for reuse. */
	public function stopAll():Void {
		ensureLive();
		var failure:Dynamic = null;
		while (active.length > 0) {
			var voice = active.pop();
			try {
				if (!voice.isDisposed())
					voice.stop();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
			if (!voice.isDisposed())
				available.push(voice);
		}
		if (failure != null)
			throw failure;
	}

	/** Stops one active cue voice and makes it available for reuse. */
	public function stop(voice:Voice):Bool {
		ensureLive();
		if (voice == null)
			return false;
		var index = active.indexOf(voice);
		if (index < 0)
			return false;
		active.splice(index, 1);
		try {
			if (!voice.isDisposed())
				voice.stop();
		} catch (error:Dynamic) {
			if (!voice.isDisposed())
				available.push(voice);
			throw error;
		}
		if (!voice.isDisposed())
			available.push(voice);
		return true;
	}

	/** Returns the number of voices currently playing or virtualized. */
	public function activeVoiceCount():Int {
		ensureLive();
		reapDisposedVoices();
		return active.length;
	}

	/** Returns the number of native voices retained by this cue. */
	public function voiceCount():Int {
		ensureLive();
		reapDisposedVoices();
		return voices.length;
	}

	/** Releases all pooled voices and the event subscription. */
	public function dispose():Void {
		if (disposed)
			return;
		subscription.dispose();
		var failure:Dynamic = null;
		for (voice in voices) {
			try {
				if (!voice.isDisposed())
					voice.stop();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
			try {
				if (!voice.isDisposed())
					voice.dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		active.resize(0);
		available.resize(0);
		voices.resize(0);
		voiceVariants.resize(0);
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
			case AudioVoiceLoadFailed(source, _): discard(source);
			case AudioVoiceStreamFailed(source, _): discard(source);
			case _: return;
		}
	}

	function release(source:NativeKit.Handle):Void {
		for (index in 0...active.length) {
			var voice = active[index];
			if (!voice.isDisposed() && voice.nativeHandle().rawValue() == source.rawValue()) {
				active.splice(index, 1);
				if (!available.contains(voice))
					available.push(voice);
				return;
			}
		}
	}

	function discard(source:NativeKit.Handle):Void {
		for (index in 0...voices.length) {
			var voice = voices[index];
			if (!voice.isDisposed() && voice.nativeHandle().rawValue() == source.rawValue()) {
				removeVoice(voice);
				voice.dispose();
				return;
			}
		}
	}

	function takeAvailable(variantIndex:Int):Null<Voice> {
		var index = available.length - 1;
		while (index >= 0) {
			var candidate = available[index];
			if (candidate.isDisposed()) {
				removeVoice(candidate);
				index--;
				continue;
			}
			if (voiceVariant(candidate) == variantIndex) {
				available.splice(index, 1);
				return candidate;
			}
			index--;
		}
		return null;
	}

	function stealOldest(variantIndex:Int):Null<Voice> {
		if (active.length == 0)
			return null;
		var candidate = active[0];
		active.splice(0, 1);
		try {
			if (!candidate.isDisposed())
				candidate.stop();
		} catch (error:Dynamic) {
			active.insert(0, candidate);
			throw error;
		}
		if (candidate.isDisposed()) {
			removeVoice(candidate);
			return null;
		}
		if (voiceVariant(candidate) == variantIndex)
			return candidate;
		removeVoice(candidate);
		candidate.dispose();
		return null;
	}

	function removeVoice(voice:Voice):Void {
		var index = voices.indexOf(voice);
		if (index >= 0) {
			voices.splice(index, 1);
			voiceVariants.splice(index, 1);
		}
		active.remove(voice);
		available.remove(voice);
	}

	function voiceVariant(voice:Voice):Int {
		var index = voices.indexOf(voice);
		return index < 0 ? -1 : voiceVariants[index];
	}

	function reapDisposedVoices():Void {
		var index = voices.length - 1;
		while (index >= 0) {
			var voice = voices[index];
			if (voice.isDisposed())
				removeVoice(voice);
			index--;
		}
	}

	function chooseVariant():Int {
		if (selection == AudioCueSelection.RoundRobin) {
			var result = nextVariant;
			nextVariant = (nextVariant + 1) % variants.length;
			return result;
		}
		return Std.random(variants.length);
	}

	function randomRange(minimum:Float, maximum:Float):Float {
		if (minimum == maximum)
			return minimum;
		return minimum + (maximum - minimum) * (Std.random(1000000) / 1000000.0);
	}

	static function validateOptions(options:AudioCueOptions):Void {
		if (options.voiceOptions == null)
			throw "Audio cue voice options must not be null";
		if (options.maxVoices < 0)
			throw "Audio cue maximum voice count must be non-negative";
		if (options.priority < 0)
			throw "Audio cue priority must be non-negative";
		if (!finite(options.volumeMin) || !finite(options.volumeMax) || options.volumeMin < 0.0 ||
			options.volumeMax < options.volumeMin)
			throw "Audio cue volume range must be finite, non-negative, and ordered";
		if (!finite(options.pitchMin) || !finite(options.pitchMax) || options.pitchMin <= 0.0 ||
			options.pitchMax < options.pitchMin)
			throw "Audio cue pitch range must be finite, positive, and ordered";
		if (options.selection != AudioCueSelection.Random &&
			options.selection != AudioCueSelection.RoundRobin)
			throw "Audio cue selection strategy is invalid";
		if (options.overflow != AudioCueOverflow.Drop &&
			options.overflow != AudioCueOverflow.StealOldest)
			throw "Audio cue overflow policy is invalid";
	}

	function validatePlayback(effectiveVolume:Float, effectivePitch:Float,
		effectivePriority:Int):Void {
		if (!finite(volume) || volume < 0.0 || !finite(effectiveVolume) || effectiveVolume < 0.0)
			throw "Audio cue volume must be finite and non-negative";
		if (!finite(pitch) || pitch <= 0.0 || !finite(effectivePitch) || effectivePitch <= 0.0)
			throw "Audio cue pitch must be finite and positive";
		if (effectivePriority < 0)
			throw "Audio cue priority must be non-negative";
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

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	function ensureLive():Void {
		if (disposed)
			throw "Audio cue has been disposed";
	}
}
