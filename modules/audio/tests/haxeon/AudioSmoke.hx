import NativeKit;
import haxe.io.Bytes;
import NativeKitEventValue;
import NativeKitEvents.NativeKitEventSubscription;
import nativekit.audio.Bus;
import nativekit.audio.Clip;
import nativekit.audio.Mixer;
import nativekit.audio.Voice;
import nativekit.audio.VoiceOptions;
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
		var init = new InitOptions();
		init.set_api_version(NativeKit.nk_api_version());
		var runtime = NativeKitRuntime.start(init);
		var bus:Bus = null;
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
			var sampleRate = Mixer.sampleRate();
			if (sampleRate <= 0)
				throw "Haxe audio sample rate was invalid";
			var nowFrames = Mixer.timeFrames();
			bus = Bus.create();
			bus.setVolume(0.5);
			if (bus.volume() != 0.5)
				throw "Haxe audio bus volume did not round-trip";
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
		} catch (error:Dynamic) {
			if (completionSubscription != null)
				completionSubscription.dispose();
			if (first != null)
				first.dispose();
			if (second != null)
				second.dispose();
			if (completion != null)
				completion.dispose();
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
