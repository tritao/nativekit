import NativeKit;
import NativeKitAudio;
import NativeKitEventDecoderTests;
import haxe.io.Bytes;
import NativeKitEventValue;
import NativeKitEvents.NativeKitEventSubscription;
import nativekit.audio.Bus;
import nativekit.audio.BusEffect;
import nativekit.audio.Clip;
import nativekit.audio.Cone;
import nativekit.audio.DistanceLimits;
import nativekit.audio.GainLimits;
import nativekit.audio.Mixer;
import nativekit.audio.Voice;
import nativekit.audio.VoiceOptions;
import nativekit.audio.Vector3;
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
		var clip:Clip = null;
		var first:Voice = null;
		var second:Voice = null;
		var completion:Voice = null;
		var completionSubscription:NativeKitEventSubscription = null;
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
			bus = Bus.create();
			bus.setVolume(0.5);
			if (bus.volume() != 0.5)
				throw "Haxe audio bus volume did not round-trip";
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
			var options = new VoiceOptions(bus);
			options.looping = true;
			first = clip.createVoice(options);
			second = clip.createVoice(options);
			completion = clip.createVoice(new VoiceOptions(bus));
			if (first.nativeHandle().rawValue() == second.nativeHandle().rawValue())
				throw "Haxe audio voices did not receive independent handles";
			if (!completion.isReady())
				throw "Haxe synchronous audio voice did not start ready";
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
			if (!lowPass.isDisposed())
				throw "Haxe audio bus did not release its child effect";
			lowPass = null;
		} catch (error:Dynamic) {
			if (completionSubscription != null)
				completionSubscription.dispose();
			if (first != null)
				first.dispose();
			if (second != null)
				second.dispose();
			if (completion != null)
				completion.dispose();
			if (lowPass != null)
				lowPass.dispose();
			if (highPass != null)
				highPass.dispose();
			if (delay != null)
				delay.dispose();
			if (clip != null)
				clip.dispose();
			if (bus != null)
				bus.dispose();
			runtime.dispose();
			throw error;
		}
		runtime.dispose();
	}
}
