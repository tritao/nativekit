import haxe.io.Bytes;

/** Frame-scoped layout geometry returned as one batch by NativeUI. */
class ResolvedLayoutItem {
	public final id:Int;
	public final x:Float;
	public final y:Float;
	public final width:Float;
	public final height:Float;

	public function new(id:Int, x:Float, y:Float, width:Float, height:Float) {
		this.id = id;
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
	}

	public inline function bounds():Rect
		return new Rect(x, y, width, height);

	public static function decode(bytes:Bytes, offset:Int):ResolvedLayoutItem {
		var recordBytes = NativeKitUIConstants.NKUI_LAYOUT_RESOLVED_ITEM_BYTES;
		if (offset < 0 || offset > bytes.length || recordBytes > bytes.length - offset)
			throw "Resolved layout item is outside the geometry snapshot";
		if (bytes.getInt32(offset) != recordBytes)
			throw "Resolved layout item has an unsupported record size";
		return new ResolvedLayoutItem(bytes.getInt32(offset + 4), readFloat(bytes, offset + 8),
			readFloat(bytes, offset + 12), readFloat(bytes, offset + 16), readFloat(bytes, offset + 20));
	}

	static inline function readFloat(bytes:Bytes, offset:Int):Float
		return floatFromBits(bytes.getInt32(offset));

	static function floatFromBits(bits:Int):Float {
		var sign = (bits >>> 31) == 0 ? 1.0 : -1.0;
		var exponent = (bits >>> 23) & 0xff;
		var fraction = bits & 0x7fffff;
		if (exponent == 255)
			throw "Native layout returned a non-finite geometry value";
		if (exponent == 0)
			return sign * fraction * Math.pow(2.0, -149.0);
		return sign * (0x800000 | fraction) * Math.pow(2.0, exponent - 150.0);
	}
}
