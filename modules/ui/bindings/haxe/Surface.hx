/** A host-owned native surface capability accepted by Renderer. */
import NativeKit.SurfaceHandle;
import NativeKitSurface;

class Surface {
	final value:SurfaceHandle;

	private function new(value:SurfaceHandle)
		this.value = value;

	/** Adapts a surface handle created by the platform/NativeKit host layer. */
	public static function fromNativeHandle(handle:SurfaceHandle):Surface
		return new Surface(handle);

	public static function fromNativeSurface(surface:NativeKitSurface):Surface
		return new Surface(surface.nativeHandle());

	@:allow(Renderer)
	@:allow(LayoutSession)
	private function nativeHandle():SurfaceHandle
		return value;
}
