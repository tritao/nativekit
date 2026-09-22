package nativekit.gpu;

import nativekit.ffi.NativeKitGpu;

/** Portable resource and binding limits exposed by one renderer. */
class Limits {
	public final maxTextureSize:Int;
	public final maxArrayLayers:Int;
	public final maxVertexAttributes:Int;
	public final maxColorAttachments:Int;
	public final maxTextureBindings:Int;
	public final maxStorageBufferBindings:Int;
	public final maxStorageImageBindings:Int;
	public final maxCubeSize:Int;

	private function new(value:nkgpu_limits) {
		maxTextureSize = value.get_max_texture_size();
		maxArrayLayers = value.get_max_array_layers();
		maxVertexAttributes = value.get_max_vertex_attributes();
		maxColorAttachments = value.get_max_color_attachments();
		maxTextureBindings = value.get_max_texture_bindings();
		maxStorageBufferBindings = value.get_max_storage_buffer_bindings();
		maxStorageImageBindings = value.get_max_storage_image_bindings();
		maxCubeSize = value.get_max_cube_size();
	}

	@:allow(Renderer)
	static function fromNative(value:nkgpu_limits):Limits
		return new Limits(value);
}
