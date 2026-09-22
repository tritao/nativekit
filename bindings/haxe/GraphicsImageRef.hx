import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;

/** An explicitly retained, backend-neutral sampled graphics image reference. */
class GraphicsImageRef {
	private var value:GraphicsImage;
	private var disposed:Bool;
	public final width:Int;
	public final height:Int;
	public final api:GraphicsApi;

	private function new(value:GraphicsImage, width:Int, height:Int, api:GraphicsApi) {
		this.value = value;
		this.width = width;
		this.height = height;
		this.api = api;
		disposed = false;
	}

	/**
	 * Retains a borrowed core graphics image handle and associates its known
	 * producer metadata.
	 * The returned value owns its reference independently of the original
	 * producer and must be released with dispose(). The caller must supply the
	 * dimensions and API reported by that producer.
	 */
	@:allow(nativekit.gpu.Image, nativekit.gpu.RenderTarget, nativekit.scene.SceneRenderer)
	private static function fromBorrowedHandle(value:GraphicsImage, width:Int, height:Int,
		api:GraphicsApi):GraphicsImageRef {
		if (width <= 0 || height <= 0)
			throw "Graphics image dimensions must be positive";
		NativeKit.nk_graphics_image_retain_checked(value);
		return new GraphicsImageRef(value, width, height, api);
	}

	/** Retains a borrowed image returned across an ID-only module boundary. */
	@:allow(nativekit.scene.SceneRenderer)
	private static function fromBorrowedId(id:Int, width:Int, height:Int):GraphicsImageRef {
		if (id == 0)
			throw "Graphics image ID must be non-zero";
		var value = new GraphicsImage(id);
		var queried = NativeKit.nk_graphics_image_get_info(value);
		if (queried.status != Result.Ok)
			throw "Could not query the borrowed graphics image";
		return fromBorrowedHandle(value, width, height, queried.out_info.get_api());
	}

	/** Creates another independently owned reference to this image. */
	public function retain():GraphicsImageRef {
		ensureLive();
		return fromBorrowedHandle(value, width, height, api);
	}

	/** Releases this reference. Repeated disposal is safe. */
	public function dispose():Void {
		if (disposed)
			return;
		NativeKit.nk_graphics_image_release_checked(value);
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(GraphicsSurface)
	private function nativeHandle():GraphicsImage {
		ensureLive();
		return value;
	}

	function ensureLive():Void {
		if (disposed)
			throw "Graphics image reference has been disposed";
	}
}
