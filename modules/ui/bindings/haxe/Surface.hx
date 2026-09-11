/** A host-owned native surface capability accepted by Renderer. */
class Surface {
	final value:Int;

	private function new(value:Int)
		this.value = value;

	/** Adapts a surface handle created by the platform/NativeKit host layer. */
	public static function fromNativeHandle(handle:Int):Surface
		return new Surface(handle);

	@:allow(Renderer)
	@:allow(LayoutSession)
	private function nativeHandle():Int
		return value;
}
