import haxe.io.Bytes;

/** Frame-scoped layout, clipping, content, visibility, and transform geometry. */
class ResolvedLayoutItem {
	public final id:Int;
	public final flags:Int;
	public final x:Float;
	public final y:Float;
	public final width:Float;
	public final height:Float;
	public final clipBounds:Rect;
	public final contentBounds:Rect;
	public final transform:Transform2D;
	public final baseline:Float;

	public function new(id:Int, flags:Int, x:Float, y:Float, width:Float, height:Float,
			clipBounds:Rect, contentBounds:Rect, transform:Transform2D, baseline:Float) {
		this.id = id;
		this.flags = flags;
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
		this.clipBounds = clipBounds;
		this.contentBounds = contentBounds;
		this.transform = transform;
		this.baseline = baseline;
	}

	public var visible(get, never):Bool;
	inline function get_visible():Bool
		return (flags & 1) != 0;

	public var hasBaseline(get, never):Bool;
	inline function get_hasBaseline():Bool
		return (flags & 2) != 0;

	public inline function bounds():Rect
		return new Rect(x, y, width, height);

	/** The node's local space always starts at its own top-left corner. */
	public inline function localBounds():Rect
		return new Rect(0.0, 0.0, width, height);

	/** Converts a point from this node's local space into viewport space. */
	public function localToViewport(point:Point):Point {
		if (point == null)
			throw "Local points cannot be null";
		return transform.transformPoint(new Point(x + point.x, y + point.y));
	}

	/** Attempts to convert a viewport point into this node's local space. */
	public function tryViewportToLocal(point:Point):Null<Point> {
		if (point == null)
			throw "Viewport points cannot be null";
		var layoutPoint = transform.tryInverse();
		if (layoutPoint == null)
			return null;
		var absolute = layoutPoint.transformPoint(point);
		return new Point(absolute.x - x, absolute.y - y);
	}

	/** Converts a viewport point into this node's local space. */
	public function viewportToLocal(point:Point):Point {
		var result = tryViewportToLocal(point);
		if (result == null)
			throw "Resolved layout transform is not invertible";
		return result;
	}

	/** Returns the axis-aligned viewport bounds of this transformed node. */
	public function viewportBounds():Rect {
		var topLeft = localToViewport(new Point(0.0, 0.0));
		var topRight = localToViewport(new Point(width, 0.0));
		var bottomLeft = localToViewport(new Point(0.0, height));
		var bottomRight = localToViewport(new Point(width, height));
		var left = Math.min(Math.min(topLeft.x, topRight.x), Math.min(bottomLeft.x, bottomRight.x));
		var top = Math.min(Math.min(topLeft.y, topRight.y), Math.min(bottomLeft.y, bottomRight.y));
		var right = Math.max(Math.max(topLeft.x, topRight.x), Math.max(bottomLeft.x, bottomRight.x));
		var bottom = Math.max(Math.max(topLeft.y, topRight.y), Math.max(bottomLeft.y, bottomRight.y));
		return new Rect(left, top, right - left, bottom - top);
	}

	/** Returns the transformed bounds intersected with the resolved clip. */
	public function clippedViewportBounds():Rect {
		var bounds = viewportBounds();
		var left = Math.max(bounds.x, clipBounds.x);
		var top = Math.max(bounds.y, clipBounds.y);
		var right = Math.min(bounds.x + bounds.width, clipBounds.x + clipBounds.width);
		var bottom = Math.min(bounds.y + bounds.height, clipBounds.y + clipBounds.height);
		return new Rect(left, top, Math.max(0.0, right - left),
			Math.max(0.0, bottom - top));
	}

	/** Converts a viewport point back to this node's pre-transform layout space. */
	public function viewportToLayout(x:Float, y:Float):Point {
		var result = transform.tryInverse();
		if (result == null)
			throw "Resolved layout transform is not invertible";
		return result.transformPoint(new Point(x, y));
	}

	/** Checks visibility, inherited clipping, and the transformed node bounds. */
	public function hitTest(x:Float, y:Float):Bool {
		if (!visible || !contains(clipBounds, x, y))
			return false;
		var point = tryViewportToLocal(new Point(x, y));
		return point != null && point.x >= 0.0 && point.y >= 0.0 && point.x <= width &&
			point.y <= height;
	}

	public static function decode(bytes:Bytes, offset:Int):ResolvedLayoutItem {
		var recordBytes = NativeKitUIConstants.NKUI_LAYOUT_RESOLVED_ITEM_BYTES;
		if (offset < 0 || offset > bytes.length || recordBytes > bytes.length - offset)
			throw "Resolved layout item is outside the geometry snapshot";
		if (bytes.getInt32(offset) != recordBytes)
			throw "Resolved layout item has an unsupported record size";
		var flags = bytes.getInt32(offset + 8);
		if ((flags & ~3) != 0)
			throw "Native layout returned unsupported resolved flags";
		return new ResolvedLayoutItem(bytes.getInt32(offset + 4), flags,
			readFloat(bytes, offset + 12), readFloat(bytes, offset + 16),
			readFloat(bytes, offset + 20), readFloat(bytes, offset + 24),
			new Rect(readFloat(bytes, offset + 28), readFloat(bytes, offset + 32),
				readFloat(bytes, offset + 36), readFloat(bytes, offset + 40)),
			new Rect(readFloat(bytes, offset + 44), readFloat(bytes, offset + 48),
				readFloat(bytes, offset + 52), readFloat(bytes, offset + 56)),
			new Transform2D(readFloat(bytes, offset + 60), readFloat(bytes, offset + 64),
				readFloat(bytes, offset + 68), readFloat(bytes, offset + 72),
				readFloat(bytes, offset + 76), readFloat(bytes, offset + 80)),
			readFloat(bytes, offset + 84));
	}

	static inline function contains(rect:Rect, x:Float, y:Float):Bool
		return x >= rect.x && y >= rect.y && x <= rect.x + rect.width && y <= rect.y + rect.height;

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
