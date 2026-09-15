package nativekit.resource;

import NativeKit;
import NativeKitError;

/** Describes a URI-backed NativeKit resource for NativeKit APIs. */
class Resource {
	public final flags:NativeKit.ResourceFlags;
	public final uri:String;
	public final mimeType:Null<String>;
	public final displayName:Null<String>;

	public function new(uri:String, ?mimeType:String, ?displayName:String,
		?flags:NativeKit.ResourceFlags) {
		if (uri == null || uri.length == 0)
			throw "NativeKit resource URI must not be empty";
		this.flags = flags == null ? NativeKit.ResourceFlags.Readable : flags;
		this.uri = uri;
		this.mimeType = mimeType;
		this.displayName = displayName;
	}

	/** Returns the ABI value for a synchronous NativeKit call. */
	public function nativeValue():NativeKit.ResourceValue {
		var value = new NativeKit.ResourceValue();
		value.set_struct_size(NativeKit.ResourceValue.size());
		value.set_flags(flags);
		value.set_uri(uri);
		value.set_mime_type(mimeType);
		value.set_display_name(displayName);
		return value;
	}

	/** Cancels a pending generic asynchronous resource load. */
	public static function cancelLoad(request:haxe.Int64):Void {
		var status = NativeKit.nk_resource_load_cancel(request);
		if (status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "resource.cancelLoad", NativeKit.nk_last_error());
	}
}
