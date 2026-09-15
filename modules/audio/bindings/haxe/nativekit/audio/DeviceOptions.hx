package nativekit.audio;

import NativeKitAudio;

/** Configures the process-wide playback device before the first audio use. */
class DeviceOptions {
	/** Selects the backend default playback device. */
	public static inline var DEFAULT_DEVICE:Int = -1;

	/** Playback device index returned by Mixer.deviceCount/deviceName. */
	public var playbackDeviceIndex:Int = DEFAULT_DEVICE;
	/** Requested output sample rate, or zero for the backend default. */
	public var sampleRate:Int = 0;
	/** Requested output channel count, or zero for the backend default. */
	public var channels:Int = 0;
	/** Requested device period in PCM frames, or zero for the backend default. */
	public var periodSizeInFrames:Int = 0;
	/** Requested device period in milliseconds, or zero for the backend default. */
	public var periodSizeInMilliseconds:Int = 0;
	/** Prevents the device from starting until Mixer.startDevice is called. */
	public var noAutoStart:Bool = false;

	public function new() {}

	@:allow(nativekit.audio.Mixer)
	private function nativeValue():NativeKitAudio.NativeDeviceOptions {
		var result = new NativeKitAudio.NativeDeviceOptions();
		result.set_struct_size(NativeKitAudio.NativeDeviceOptions.size());
		result.set_playback_device_index(playbackDeviceIndex);
		result.set_sample_rate(sampleRate);
		result.set_channels(channels);
		result.set_period_size_in_frames(periodSizeInFrames);
		result.set_period_size_in_milliseconds(periodSizeInMilliseconds);
		result.set_no_auto_start(noAutoStart);
		return result;
	}
}
