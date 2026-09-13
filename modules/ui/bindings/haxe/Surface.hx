/** A host-owned native surface capability accepted by Renderer. */
import NativeKit.Handle;
import NativeKitSurface;

class Surface {
	final value:Handle;

	private function new(value:Handle)
		this.value = value;

	/** Adapts a surface handle created by the platform/NativeKit host layer. */
	public static function fromNativeHandle(handle:Handle):Surface
		return new Surface(handle);

	public static function fromNativeSurface(surface:NativeKitSurface):Surface
		return new Surface(surface.nativeHandle());

	@:allow(Renderer)
	@:allow(LayoutSession)
	private function nativeHandle():Handle
		return value;
}
