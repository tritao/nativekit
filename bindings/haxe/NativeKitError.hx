import NativeKit.Result;

/** A failed NativeKit operation with its machine-readable result and native diagnostic. */
class NativeKitError extends haxe.Exception {
	public final result:Result;
	public final operation:String;
	public final diagnostic:Null<String>;

	public function new(result:Result, operation:String, diagnostic:Null<String>) {
		super(messageFor(result, operation, diagnostic));
		this.result = result;
		this.operation = operation;
		this.diagnostic = diagnostic;
	}

	static function messageFor(result:Result, operation:String, diagnostic:Null<String>):String
		return '$operation failed ($result)${diagnostic == null || diagnostic.length == 0 ? "" : ": " + diagnostic}';
}
