package nativekit.gpu;

import nativekit.ffi.NativeKitGpu;
import nativekit.gpu.Enums.ImageFormat;
import nativekit.gpu.Enums.ImageType;
import nativekit.gpu.Enums.ImageUsage;

/** Portable descriptor for a NativeKit GPU image. */
class ImageDesc {
	public var width:Int;
	public var height:Int;
	public var format:ImageFormat;
	public var usage:ImageUsage;
	public var mipCount:Int;
	public var sampleCount:Int;
	public var layerCount:Int;
	public var rowPitch:Int;
	public var dynamicUpdate:Bool;
	public var type:ImageType;

	public function new(width:Int, height:Int, format:ImageFormat = ImageFormat.Rgba8,
		usage:ImageUsage = ImageUsage.Sampled) {
		this.width = width;
		this.height = height;
		this.format = format;
		this.usage = usage;
		mipCount = 1;
		sampleCount = 1;
		layerCount = 1;
		rowPitch = 0;
		dynamicUpdate = false;
		type = ImageType.Auto;
	}

	@:allow(Image)
	function nativeValue():nkgpu_image_desc {
		var value = new nkgpu_image_desc();
		value.set_struct_size(56);
		value.set_width(width);
		value.set_height(height);
		value.set_format(format);
		value.set_usage(usage);
		value.set_mip_count(mipCount);
		value.set_sample_count(sampleCount);
		value.set_layer_count(layerCount);
		value.set_data_size(0);
		value.set_row_pitch(rowPitch);
		value.set_dynamic_update(dynamicUpdate ? 1 : 0);
		value.set_type(type);
		return value;
	}
}
