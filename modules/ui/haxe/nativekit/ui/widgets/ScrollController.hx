package nativekit.ui.widgets;

/** Haxe-owned scroll offsets and resolved viewport/content metrics. */
class ScrollController {
	public var offsetX(default, null):Float;
	public var offsetY(default, null):Float;
	public var viewportWidth(default, null):Float;
	public var viewportHeight(default, null):Float;
	public var contentWidth(default, null):Float;
	public var contentHeight(default, null):Float;
	var hasMetrics:Bool;
	var changed:ScrollController->Void;

	public function new(offsetX:Float = 0.0, offsetY:Float = 0.0) {
		if (!finiteNonNegative(offsetX) || !finiteNonNegative(offsetY))
			throw "Scroll offsets must be finite and non-negative";
		this.offsetX = offsetX;
		this.offsetY = offsetY;
		viewportWidth = 0.0;
		viewportHeight = 0.0;
		contentWidth = 0.0;
		contentHeight = 0.0;
		hasMetrics = false;
		changed = null;
	}

	public var maxScrollX(get, never):Float;
	inline function get_maxScrollX():Float
		return Math.max(0.0, contentWidth - viewportWidth);

	public var maxScrollY(get, never):Float;
	inline function get_maxScrollY():Float
		return Math.max(0.0, contentHeight - viewportHeight);

	/** Sets an absolute offset, clamped to the latest resolved content metrics. */
	public function jumpTo(x:Float, y:Float):Bool {
		if (!Math.isFinite(x) || !Math.isFinite(y))
			throw "Scroll offsets must be finite";
		var nextX = Math.max(0.0, hasMetrics ? Math.min(x, maxScrollX) : x);
		var nextY = Math.max(0.0, hasMetrics ? Math.min(y, maxScrollY) : y);
		if (nextX == offsetX && nextY == offsetY)
			return false;
		offsetX = nextX;
		offsetY = nextY;
		notifyChanged();
		return true;
	}

	/** Applies logical-pixel movement and returns whether the offset changed. */
	public function scrollBy(x:Float, y:Float):Bool
		return jumpTo(offsetX + x, offsetY + y);

	@:allow(nativekit.ui.widgets.ScrollView)
	function bind(callback:ScrollController->Void):Void
		changed = callback;

	@:allow(nativekit.ui.widgets.ScrollView)
	function updateMetrics(viewportWidth:Float, viewportHeight:Float,
			contentWidth:Float, contentHeight:Float):Void {
		if (!finiteNonNegative(viewportWidth) || !finiteNonNegative(viewportHeight) ||
			!finiteNonNegative(contentWidth) || !finiteNonNegative(contentHeight))
			throw "Scroll metrics must be finite and non-negative";
		this.viewportWidth = viewportWidth;
		this.viewportHeight = viewportHeight;
		this.contentWidth = contentWidth;
		this.contentHeight = contentHeight;
		hasMetrics = true;
		var nextX = Math.min(offsetX, maxScrollX);
		var nextY = Math.min(offsetY, maxScrollY);
		if (nextX != offsetX || nextY != offsetY) {
			offsetX = nextX;
			offsetY = nextY;
			notifyChanged();
		}
	}

	function notifyChanged():Void {
		if (changed != null)
			changed(this);
	}

	static function finiteNonNegative(value:Float):Bool
		return Math.isFinite(value) && value >= 0.0;
}
