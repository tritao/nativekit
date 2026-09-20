package nativekit.gpu;

import NativeKitGpu;

/** Portable operations supported by one image format on a renderer. */
class ImageFormatSupport {
	public final sampled:Bool;
	public final filter:Bool;
	public final renderTarget:Bool;
	public final blend:Bool;
	public final multisample:Bool;
	public final depthStencil:Bool;
	public final storage:Bool;

	private function new(value:nkgpu_image_format_support) {
		sampled = value.get_sampled() != 0;
		filter = value.get_filter() != 0;
		renderTarget = value.get_render_target() != 0;
		blend = value.get_blend() != 0;
		multisample = value.get_multisample() != 0;
		depthStencil = value.get_depth_stencil() != 0;
		storage = value.get_storage() != 0;
	}

	@:allow(Renderer)
	static function fromNative(value:nkgpu_image_format_support):ImageFormatSupport
		return new ImageFormatSupport(value);
}
