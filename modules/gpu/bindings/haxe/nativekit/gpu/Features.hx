package nativekit.gpu;

import NativeKitGpu;

/** Optional GPU capabilities exposed by one renderer. */
class Features {
	public final mrtCount:Int;
	public final maxSamples:Int;
	public final storageBuffer:Bool;
	public final storageImage:Bool;
	public final compute:Bool;
	public final instancing:Bool;
	public final bufferCopy:Bool;
	public final imageCopy:Bool;
	public final imageReadback:Bool;
	public final bufferReadback:Bool;
	public final timestamps:Bool;

	private function new(value:nkgpu_features) {
		mrtCount = value.get_mrt_count();
		maxSamples = value.get_max_samples();
		storageBuffer = value.get_storage_buffer() != 0;
		storageImage = value.get_storage_image() != 0;
		compute = value.get_compute() != 0;
		instancing = value.get_instancing() != 0;
		bufferCopy = value.get_buffer_copy() != 0;
		imageCopy = value.get_image_copy() != 0;
		imageReadback = value.get_image_readback() != 0;
		bufferReadback = value.get_buffer_readback() != 0;
		timestamps = value.get_timestamps() != 0;
	}

	@:allow(Renderer)
	static function fromNative(value:nkgpu_features):Features
		return new Features(value);
}
