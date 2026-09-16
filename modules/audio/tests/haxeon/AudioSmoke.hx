import NativeKit;
import NativeKitAudio;
import NativeKitEventDecoderTests;
import haxe.io.Bytes;
import NativeKitEventValue;
import NativeKitEvents.NativeKitEventSubscription;
import nativekit.audio.AudioCue;
import nativekit.audio.AudioCueOptions;
import nativekit.audio.AudioEmitter;
import nativekit.audio.AudioPlayOptions;
import nativekit.audio.Bus;
import nativekit.audio.BusConcurrencyOptions;
import nativekit.audio.BusEffect;
import nativekit.audio.Clip;
import nativekit.audio.Cone;
import nativekit.audio.DistanceLimits;
import nativekit.audio.DeviceOptions;
import nativekit.audio.GainLimits;
import nativekit.audio.Mixer;
import nativekit.audio.MixSnapshot;
import nativekit.audio.Voice;
import nativekit.audio.VoiceOptions;
import nativekit.audio.Vector3;
import nativekit.audio.Enums.VoiceStealPolicy;
import nativekit.resource.Resource;

class AudioSmoke {
	static function tinyWav():Bytes {
		var bytes = Bytes.alloc(52);
		var text = "RIFF";
		for (index in 0...text.length)
			bytes.set(index, text.charCodeAt(index));
		bytes.set(4, 44);
		for (index in 0...4)
			bytes.set(8 + index, "WAVE".charCodeAt(index));
		for (index in 0...4)
			bytes.set(12 + index, "fmt ".charCodeAt(index));
		bytes.set(16, 16);
		bytes.set(20, 1);
		bytes.set(22, 1);
		bytes.set(24, 0x40);
		bytes.set(25, 0x1f);
		bytes.set(28, 0x40);
		bytes.set(29, 0x1f);
		bytes.set(32, 1);
		bytes.set(34, 8);
		for (index in 0...4)
			bytes.set(36 + index, "data".charCodeAt(index));
		bytes.set(40, 8);
		for (index in 0...8)
			bytes.set(44 + index, 128);
		return bytes;
	}

	static function main():Void {
		if (!NativeKitEventDecoderTests.run())
			throw "NativeKit audio event decoder tests failed";
		var init = new InitOptions();
		init.set_api_version(NativeKit.nk_api_version());
		var runtime = NativeKitRuntime.start(init);
		var bus:Bus = null;
		var lowPass:BusEffect = null;
		var highPass:BusEffect = null;
		var delay:BusEffect = null;
		var parentBus:Bus = null;
		var baseSnapshot:MixSnapshot = null;
		var duckSnapshot:MixSnapshot = null;
		var clip:Clip = null;
		var cue:AudioCue = null;
		var cueVoice:Voice = null;
		var polyCue:AudioCue = null;
		var polyVoice:Voice = null;
		var emitter:AudioEmitter = null;
		var emitterVoice:Voice = null;
		var first:Voice = null;
		var second:Voice = null;
		var completion:Voice = null;
		var transitionSubscription:NativeKitEventSubscription = null;
		var completionSubscription:NativeKitEventSubscription = null;
		var stolenTransition = false;
		var virtualizedTransition = false;
		var resumedTransition = false;
		try {
			var resource = new Resource("file:///nativekit-audio-smoke.wav", "audio/wav", "smoke.wav");
			if (resource.uri != "file:///nativekit-audio-smoke.wav" || resource.mimeType != "audio/wav" ||
				resource.displayName != "smoke.wav")
				throw "Haxe resource descriptor did not retain metadata";
			var nativeResource = resource.nativeValue();
			if (nativeResource.get_struct_size() != NativeKit.ResourceValue.size() ||
				nativeResource.get_flags() != resource.flags || nativeResource.get_uri() != resource.uri ||
				nativeResource.get_mime_type() != resource.mimeType ||
				nativeResource.get_display_name() != resource.displayName)
				throw "Haxe resource descriptor did not build its ABI value";
			var deviceCount = Mixer.deviceCount();
			if (deviceCount <= 0)
				throw "Haxe audio device enumeration returned no playback devices";
			if (Mixer.deviceName(0).length == 0)
				throw "Haxe audio device enumeration returned an empty device name";
			Mixer.deviceIsDefault(0);
			var deviceOptions = new DeviceOptions();
			deviceOptions.sampleRate = 48000;
			deviceOptions.channels = 2;
			deviceOptions.noAutoStart = true;
			Mixer.configureDevice(deviceOptions);
			if (Mixer.deviceState() != NativeKitAudio.DeviceState.Uninitialized)
				throw "Haxe audio device did not remain uninitialized before start";
			Mixer.startDevice();
			if (Mixer.deviceState() != NativeKitAudio.DeviceState.Started)
				throw "Haxe audio device did not start";
			Mixer.stopDevice();
			if (Mixer.deviceState() != NativeKitAudio.DeviceState.Stopped)
				throw "Haxe audio device did not stop";
			Mixer.restartDevice();
			if (Mixer.deviceState() != NativeKitAudio.DeviceState.Started)
				throw "Haxe audio device did not restart";
			if (Mixer.sampleRate() != 48000)
				throw "Haxe audio device configuration did not apply its sample rate";
			Mixer.setMasterVolume(0.75);
			if (Mixer.masterVolume() != 0.75)
				throw "Haxe audio master volume did not round-trip";
			Mixer.setListenerPosition(new Vector3(4.0, 2.0, -3.0));
			var listenerPosition = Mixer.listenerPosition();
			if (listenerPosition.x != 4.0 || listenerPosition.y != 2.0 || listenerPosition.z != -3.0)
				throw "Haxe audio listener position did not round-trip";
			Mixer.setListenerDirection(new Vector3(0.0, 0.0, -1.0));
			var listenerDirection = Mixer.listenerDirection();
			if (listenerDirection.x != 0.0 || listenerDirection.y != 0.0 || listenerDirection.z != -1.0)
				throw "Haxe audio listener direction did not round-trip";
			Mixer.setListenerVelocity(new Vector3(1.0, 0.0, 0.0));
			var listenerVelocity = Mixer.listenerVelocity();
			if (listenerVelocity.x != 1.0 || listenerVelocity.y != 0.0 || listenerVelocity.z != 0.0)
				throw "Haxe audio listener velocity did not round-trip";
			Mixer.setListenerWorldUp(new Vector3(0.0, 1.0, 0.0));
			var listenerWorldUp = Mixer.listenerWorldUp();
			if (listenerWorldUp.x != 0.0 || listenerWorldUp.y != 1.0 || listenerWorldUp.z != 0.0)
				throw "Haxe audio listener world up did not round-trip";
			Mixer.setListenerCone(new Cone(0.5, 1.5, 0.25));
			var listenerCone = Mixer.listenerCone();
			if (listenerCone.innerAngleRadians != 0.5 || listenerCone.outerAngleRadians != 1.5 ||
				listenerCone.outerGain != 0.25)
				throw "Haxe audio listener cone did not round-trip";
			Mixer.setSpeedOfSound(340.0);
			if (Mixer.speedOfSound() != 340.0)
				throw "Haxe audio speed of sound did not round-trip";
			var sampleRate = Mixer.sampleRate();
			if (sampleRate <= 0)
				throw "Haxe audio sample rate was invalid";
			var nowFrames = Mixer.timeFrames();
			parentBus = Bus.create();
			bus = Bus.create(parentBus);
			if (!bus.hasParent())
				throw "Haxe audio bus did not retain its parent";
			bus.setParent(null);
			if (bus.hasParent())
				throw "Haxe audio bus did not detach from its parent";
			bus.setParent(parentBus);
			if (!bus.hasParent())
				throw "Haxe audio bus did not reattach to its parent";
			bus.setVolume(0.5);
			if (bus.volume() != 0.5)
				throw "Haxe audio bus volume did not round-trip";
			baseSnapshot = MixSnapshot.create();
			baseSnapshot.captureBus(bus);
			if (baseSnapshot.busCount() != 1)
				throw "Haxe audio mix snapshot did not capture its bus";
			duckSnapshot = MixSnapshot.create();
			duckSnapshot.setBus(bus, 0.1, true);
			duckSnapshot.apply(haxe.Int64.ofInt(0));
			if (Math.abs(bus.volume() - 0.1) > 0.0001 || !bus.isMuted())
				throw "Haxe audio mix snapshot did not apply its duck target";
			baseSnapshot.apply(haxe.Int64.ofInt(0));
			if (bus.volume() != 0.5 || bus.isMuted())
				throw "Haxe audio mix snapshot did not restore its base target";
			duckSnapshot.clear();
			if (duckSnapshot.busCount() != 0)
				throw "Haxe audio mix snapshot did not clear its targets";
			duckSnapshot.dispose();
			duckSnapshot = null;
			baseSnapshot.dispose();
			baseSnapshot = null;
			lowPass = bus.addLowPass(2000.0, 2);
			highPass = bus.addHighPass(100.0, 1);
			delay = bus.addDelay(64, 0.5);
			if (lowPass.type() != NativeKitAudio.EffectType.LowPass ||
				highPass.type() != NativeKitAudio.EffectType.HighPass ||
				delay.type() != NativeKitAudio.EffectType.Delay)
				throw "Haxe audio bus effect types did not round-trip";
			if (lowPass.position() != 0 || highPass.position() != 1 || delay.position() != 2)
				throw "Haxe audio bus effect positions did not round-trip";
			highPass.setPosition(0);
			delay.setPosition(1);
			if (highPass.position() != 0 || delay.position() != 1 || lowPass.position() != 2)
				throw "Haxe audio bus effect reorder did not round-trip";
			delay.setEnabled(false);
			if (delay.isEnabled())
				throw "Haxe audio bus effect bypass did not round-trip";
			delay.setEnabled(true);
			lowPass.setLowPass(4000.0, 4);
			var lowPassSettings = lowPass.lowPass();
			if (lowPassSettings.cutoffFrequencyHz != 4000.0 || lowPassSettings.order != 4)
				throw "Haxe audio low-pass settings did not round-trip";
			highPass.setHighPass(250.0, 2);
			var highPassSettings = highPass.highPass();
			if (highPassSettings.cutoffFrequencyHz != 250.0 || highPassSettings.order != 2)
				throw "Haxe audio high-pass settings did not round-trip";
			delay.setDelayWet(0.25);
			if (delay.delayWet() != 0.25)
				throw "Haxe audio delay wet gain did not round-trip";
			delay.setDelayDry(0.75);
			if (delay.delayDry() != 0.75)
				throw "Haxe audio delay dry gain did not round-trip";
			delay.setDelayDecay(0.5);
			if (delay.delayDecay() != 0.5)
				throw "Haxe audio delay decay did not round-trip";
			highPass.dispose();
			highPass = null;
			delay.dispose();
			delay = null;
			bus.fade(Bus.CURRENT_VOLUME, 0.75, haxe.Int64.ofInt(1));
			bus.fadeAt(0.75, 0.5, haxe.Int64.ofInt(1),
				haxe.Int64.add(nowFrames, haxe.Int64.ofInt(sampleRate)));
			bus.scheduleStart(haxe.Int64.add(nowFrames, haxe.Int64.ofInt(sampleRate * 5)));
			bus.scheduleStop(haxe.Int64.add(nowFrames, haxe.Int64.ofInt(sampleRate * 6)));
			bus.clearSchedule();
			clip = Clip.fromMemory(tinyWav());
			var options = new VoiceOptions();
			options.looping = true;
			first = clip.createVoice(options);
			second = clip.createVoice(options);
			first.setBus(bus);
			second.setBus(bus);
			completion = clip.createVoice(new VoiceOptions());
			completion.setBus(bus);
			if (first.nativeHandle().rawValue() == second.nativeHandle().rawValue())
				throw "Haxe audio voices did not receive independent handles";
			if (!completion.isReady())
				throw "Haxe synchronous audio voice did not start ready";
			var concurrency = new BusConcurrencyOptions();
			concurrency.maxVoices = 1;
			bus.setConcurrency(concurrency);
			var queriedConcurrency = bus.concurrency();
			if (queriedConcurrency.maxVoices != 1 || queriedConcurrency.stealPolicy != VoiceStealPolicy.None ||
				queriedConcurrency.virtualize)
				throw "Haxe audio bus concurrency policy did not round-trip";
			first.setPriority(1);
			second.setPriority(2);
			first.start();
			var rejected = false;
			try {
				second.start();
			} catch (_:Dynamic) {
				rejected = true;
			}
			if (!rejected)
				throw "Haxe audio bus concurrency limit did not reject a voice";
			transitionSubscription = runtime.events.listen(function(value) {
				switch value {
					case AudioVoiceStolen(source) if (source.rawValue() == first.nativeHandle().rawValue()):
						stolenTransition = true;
					case AudioVoiceVirtualized(source) if (source.rawValue() == second.nativeHandle().rawValue()):
						virtualizedTransition = true;
					case AudioVoiceResumed(source) if (source.rawValue() == second.nativeHandle().rawValue()):
						resumedTransition = true;
					case _:
				}
			});
			first.stop();
			second.start();
			concurrency.stealPolicy = VoiceStealPolicy.LowestPriority;
			bus.setConcurrency(concurrency);
			second.stop();
			first.start();
			second.start();
			while (runtime.events.poll()) {}
			if (!stolenTransition)
				throw "Haxe audio voice stealing event was not delivered";
			if (first.isPlaying() || !second.isPlaying())
				throw "Haxe audio bus priority stealing did not select the lower-priority voice";
			concurrency.stealPolicy = VoiceStealPolicy.None;
			concurrency.virtualize = true;
			bus.setConcurrency(concurrency);
			second.stop();
			first.start();
			second.start();
			while (runtime.events.poll()) {}
			if (!virtualizedTransition)
				throw "Haxe audio voice virtualization event was not delivered";
			if (!second.isVirtualized() || !second.isPlaying())
				throw "Haxe audio voice virtualization did not activate";
			first.stop();
			while (runtime.events.poll()) {}
			if (!resumedTransition)
				throw "Haxe audio voice resume event was not delivered";
			if (second.isVirtualized() || !second.isPlaying())
				throw "Haxe audio virtualized voice did not resume";
			transitionSubscription.dispose();
			transitionSubscription = null;
			concurrency.maxVoices = 0;
			concurrency.virtualize = false;
			bus.setConcurrency(concurrency);
			second.stop();
			var cueOptions = new AudioCueOptions();
			cueOptions.maxVoices = 2;
			cueOptions.selection = nativekit.audio.Enums.AudioCueSelection.RoundRobin;
			cueOptions.volumeMin = 0.5;
			cueOptions.volumeMax = 0.5;
			cueOptions.pitchMin = 1.25;
			cueOptions.pitchMax = 1.25;
			cueOptions.priority = 1;
			cue = AudioCue.fromClips([clip, clip], runtime.events, bus, cueOptions);
			if (cue.variantCount() != 2 || cue.variant(0) != clip || cue.variant(1) != clip)
				throw "Haxe audio cue did not retain its clip variants";
			var playOverrides = new AudioPlayOptions();
			playOverrides.volume = 0.25;
			playOverrides.pitch = 1.5;
			playOverrides.priority = 3;
			playOverrides.position = new Vector3(1.0, 2.0, -3.0);
			cueVoice = cue.play(playOverrides);
			if (cueVoice == null || cue.activeVoiceCount() != 1 || cue.voiceCount() != 1 ||
				cue.variantIndex(cueVoice) != 0 || cueVoice.volume() != 0.25 || cueVoice.pitch() != 1.5 ||
				cueVoice.priority() != 3)
				throw "Haxe audio cue did not apply per-play overrides";
			var cuePosition = cueVoice.position();
			if (cuePosition.x != 1.0 || cuePosition.y != 2.0 || cuePosition.z != -3.0)
				throw "Haxe audio cue did not apply its per-play position";
			for (attempt in 0...20) {
				if (cue.activeVoiceCount() == 0)
					break;
				NativeKit.nk_wait_events_timeout_checked(0.05);
				while (runtime.events.poll()) {}
			}
			if (cue.activeVoiceCount() != 0)
				throw "Haxe audio cue did not retire its completed voice";
			var reusedCueVoice = cue.play();
			if (reusedCueVoice == null || cue.variantIndex(reusedCueVoice) != 1 || cue.voiceCount() != 2 ||
				reusedCueVoice.volume() != 0.5 || reusedCueVoice.pitch() != 1.25 ||
				reusedCueVoice.priority() != 1)
				throw "Haxe audio cue did not select its next variant or range values";
			var nextCueVoice = cue.play();
			if (nextCueVoice != cueVoice || cue.variantIndex(nextCueVoice) != 0 || cue.voiceCount() != 2)
				throw "Haxe audio cue did not round-robin and reuse the matching variant pool";
			cue.stopAll();
			var polyOptions = new AudioCueOptions();
			polyOptions.maxVoices = 1;
			polyOptions.voiceOptions.looping = true;
			polyCue = AudioCue.fromClips([clip], runtime.events, bus, polyOptions);
			polyVoice = polyCue.play();
			if (polyVoice == null || polyCue.play() != null)
				throw "Haxe audio cue drop overflow policy did not cap polyphony";
			polyCue.overflow = nativekit.audio.Enums.AudioCueOverflow.StealOldest;
			var stolenCueVoice = polyCue.play();
			if (stolenCueVoice != polyVoice || polyCue.activeVoiceCount() != 1)
				throw "Haxe audio cue steal overflow policy did not recycle the oldest voice";
			polyCue.stopAll();
			polyCue.dispose();
			polyCue = null;
			polyVoice = null;
			emitter = new AudioEmitter(cue, runtime.events);
			emitter.setPosition(new Vector3(3.0, 4.0, -5.0));
			emitter.setDirection(new Vector3(0.0, 0.0, 1.0));
			emitter.setVelocity(new Vector3(0.5, 0.0, -0.25));
			emitterVoice = emitter.play();
			if (emitterVoice == null || emitter.activeVoiceCount() != 1)
				throw "Haxe audio emitter did not start a cue voice";
			var emitterPosition = emitterVoice.position();
			if (emitterPosition.x != 3.0 || emitterPosition.y != 4.0 || emitterPosition.z != -5.0)
				throw "Haxe audio emitter did not apply its position";
			if (!emitter.stop(emitterVoice) || emitter.activeVoiceCount() != 0)
				throw "Haxe audio emitter did not stop its voice";
			emitter.dispose();
			emitter = null;
			emitterVoice = null;
			cue.dispose();
			cue = null;
			cueVoice = null;
			clip.dispose();
			clip = null;
			if (!first.isLooping())
				throw "Haxe audio looping state did not round-trip";
			first.setVolume(0.5);
			if (first.volume() != 0.5)
				throw "Haxe audio volume did not round-trip";
			first.setSpatializationEnabled(true);
			if (!first.isSpatializationEnabled())
				throw "Haxe audio spatialization state did not round-trip";
			first.setPosition(new Vector3(8.0, -1.0, -6.0));
			var sourcePosition = first.position();
			if (sourcePosition.x != 8.0 || sourcePosition.y != -1.0 || sourcePosition.z != -6.0)
				throw "Haxe audio voice position did not round-trip";
			first.setDirection(new Vector3(0.0, 0.0, 1.0));
			var sourceDirection = first.direction();
			if (sourceDirection.x != 0.0 || sourceDirection.y != 0.0 || sourceDirection.z != 1.0)
				throw "Haxe audio voice direction did not round-trip";
			first.setVelocity(new Vector3(-2.0, 0.0, 0.5));
			var sourceVelocity = first.velocity();
			if (sourceVelocity.x != -2.0 || sourceVelocity.y != 0.0 || sourceVelocity.z != 0.5)
				throw "Haxe audio voice velocity did not round-trip";
			first.setAttenuationModel(NativeKitAudio.AttenuationModel.Linear);
			if (first.attenuationModel() != NativeKitAudio.AttenuationModel.Linear)
				throw "Haxe audio attenuation model did not round-trip";
			first.setPositioning(NativeKitAudio.Positioning.Relative);
			if (first.positioning() != NativeKitAudio.Positioning.Relative)
				throw "Haxe audio positioning did not round-trip";
			first.setRolloff(0.75);
			if (first.rolloff() != 0.75)
				throw "Haxe audio rolloff did not round-trip";
			first.setGainLimits(0.25, 0.75);
			var gainLimits:GainLimits = first.gainLimits();
			if (gainLimits.min != 0.25 || gainLimits.max != 0.75)
				throw "Haxe audio gain limits did not round-trip";
			first.setDistanceLimits(2.0, 100.0);
			var distanceLimits:DistanceLimits = first.distanceLimits();
			if (distanceLimits.min != 2.0 || distanceLimits.max != 100.0)
				throw "Haxe audio distance limits did not round-trip";
			first.setDopplerFactor(0.5);
			if (first.dopplerFactor() != 0.5)
				throw "Haxe audio Doppler factor did not round-trip";
			first.setCone(new Cone(0.25, 1.25, 0.25));
			var sourceCone = first.cone();
			if (sourceCone.innerAngleRadians != 0.25 || sourceCone.outerAngleRadians != 1.25 ||
				sourceCone.outerGain != 0.25)
				throw "Haxe audio voice cone did not round-trip";
			first.setDirectionalAttenuationFactor(0.25);
			if (first.directionalAttenuationFactor() != 0.25)
				throw "Haxe audio directional attenuation did not round-trip";
			var fadeFrames = Std.int(sampleRate / 100);
			first.fade(Voice.CURRENT_VOLUME, 0.25, haxe.Int64.ofInt(fadeFrames));
			first.fadeAt(0.25, 0.5, haxe.Int64.ofInt(fadeFrames),
				haxe.Int64.add(nowFrames, haxe.Int64.ofInt(sampleRate)));
			first.scheduleStart(nowFrames);
			first.scheduleStop(haxe.Int64.add(nowFrames, haxe.Int64.ofInt(sampleRate * 2)));
			first.clearSchedule();
			first.start();
			second.start();
			completion.start();
			if (!bus.isPlaying())
				throw "Haxe audio bus did not observe playback";
			var completed = false;
			completionSubscription = runtime.events.listen(function(value) {
				switch value {
					case AudioVoiceComplete(source) if (source.rawValue() == completion.nativeHandle().rawValue()):
						completed = true;
					case _:
						completed = completed;
				}
			});
			for (attempt in 0...20) {
				if (completed)
					break;
				NativeKit.nk_wait_events_timeout_checked(0.05);
				runtime.events.poll();
			}
			if (!completed)
				throw "Haxe audio voice completion event was not delivered";
			completionSubscription.dispose();
			completionSubscription = null;
			bus.setMuted(true);
			if (!bus.isMuted())
				throw "Haxe audio mute state did not round-trip";
			bus.setMuted(false);
			bus.stop();
			first.stop();
			second.stop();
			completion.stop();
			first.dispose();
			first = null;
			second.dispose();
			second = null;
			completion.dispose();
			completion = null;
			bus.dispose();
			bus = null;
			parentBus.dispose();
			parentBus = null;
			if (!lowPass.isDisposed())
				throw "Haxe audio bus did not release its child effect";
			lowPass = null;
		} catch (error:Dynamic) {
			if (transitionSubscription != null)
				transitionSubscription.dispose();
			if (completionSubscription != null)
				completionSubscription.dispose();
			if (first != null)
				first.dispose();
			if (second != null)
				second.dispose();
			if (completion != null)
				completion.dispose();
			if (polyCue != null)
				polyCue.dispose();
			if (emitter != null)
				emitter.dispose();
			if (cue != null)
				cue.dispose();
			if (lowPass != null)
				lowPass.dispose();
			if (highPass != null)
				highPass.dispose();
			if (delay != null)
				delay.dispose();
			if (duckSnapshot != null)
				duckSnapshot.dispose();
			if (baseSnapshot != null)
				baseSnapshot.dispose();
			if (clip != null)
				clip.dispose();
			if (bus != null)
				bus.dispose();
			if (parentBus != null)
				parentBus.dispose();
			runtime.dispose();
			throw error;
		}
		runtime.dispose();
	}
}
