/** Immutable, fully managed snapshot passed to typed event decoders. */
class NativeKitEventContext {
	public final kind:Int;
	public final source:nk_handle;
	public final request:haxe.Int64;
	public final result:Int;
	public final flags:Int;
	public final dataCount:Int;
	public final data:haxe.io.Bytes;

	public function new(kind:Int, source:nk_handle, request:haxe.Int64, result:Int, flags:Int, dataCount:Int, data:haxe.io.Bytes) {
		this.kind = kind;
		this.source = source;
		this.request = request;
		this.result = result;
		this.flags = flags;
		this.dataCount = dataCount;
		this.data = data;
	}
}
