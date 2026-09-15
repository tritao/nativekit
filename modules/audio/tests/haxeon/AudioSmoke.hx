import NativeKit;
import haxe.io.Bytes;
import nativekit.audio.Bus;
import nativekit.audio.Clip;
import nativekit.audio.Mixer;
import nativekit.audio.Voice;
import nativekit.audio.VoiceOptions;

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
		NativeKit.nk_init_checked(init);
		var bus:Bus = null;
		var clip:Clip = null;
		var first:Voice = null;
		var second:Voice = null;
		try {
			Mixer.setMasterVolume(0.75);
			if (Mixer.masterVolume() != 0.75)
				throw "Haxe audio master volume did not round-trip";
			bus = Bus.create();
			bus.setVolume(0.5);
			if (bus.volume() != 0.5)
				throw "Haxe audio bus volume did not round-trip";
			clip = Clip.fromMemory(tinyWav());
			var options = new VoiceOptions(bus);
			options.looping = true;
			first = clip.createVoice(options);
			second = clip.createVoice(options);
			if (first.nativeHandle().rawValue() == second.nativeHandle().rawValue())
				throw "Haxe audio voices did not receive independent handles";
			clip.dispose();
			clip = null;
			if (!first.isLooping())
				throw "Haxe audio looping state did not round-trip";
			first.setVolume(0.5);
			if (first.volume() != 0.5)
				throw "Haxe audio volume did not round-trip";
			first.start();
			second.start();
			if (!bus.isPlaying())
				throw "Haxe audio bus did not observe playback";
			bus.setMuted(true);
			if (!bus.isMuted())
				throw "Haxe audio mute state did not round-trip";
			bus.setMuted(false);
			bus.stop();
			first.stop();
			second.stop();
			first.dispose();
			first = null;
			second.dispose();
			second = null;
			bus.dispose();
			bus = null;
		} catch (error:Dynamic) {
			if (first != null)
				first.dispose();
			if (second != null)
				second.dispose();
			if (clip != null)
				clip.dispose();
			if (bus != null)
				bus.dispose();
			NativeKit.nk_shutdown();
			throw error;
		}
		NativeKit.nk_shutdown();
	}
}
