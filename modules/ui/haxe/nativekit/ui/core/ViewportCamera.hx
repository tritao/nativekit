package nativekit.ui.core;

/** Camera state shared by GPU viewport widgets and application overlays. */
class ViewportCamera {
	public final minZoom:Float;
	public final maxZoom:Float;
	public var zoom(default, null):Float;
	public var panX(default, null):Float;
	public var panY(default, null):Float;
	public var revision(default, null):Int;

	public function new(zoom:Float = 1.0, panX:Float = 0.0, panY:Float = 0.0,
			minZoom:Float = 0.01, maxZoom:Float = 100.0) {
		if (!finite(zoom) || !finite(panX) || !finite(panY) || !finite(minZoom) ||
			!finite(maxZoom) || minZoom <= 0.0 || maxZoom < minZoom)
			throw "Viewport camera values must be finite and ordered";
		this.minZoom = minZoom;
		this.maxZoom = maxZoom;
		this.zoom = clamp(zoom, minZoom, maxZoom);
		this.panX = panX;
		this.panY = panY;
		revision = 1;
	}

	public function setZoom(value:Float):Bool {
		if (!finite(value))
			throw "Viewport zoom must be finite";
		var next = clamp(value, minZoom, maxZoom);
		if (next == zoom)
			return false;
		zoom = next;
		revision++;
		return true;
	}

	public function setPan(x:Float, y:Float):Bool {
		if (!finite(x) || !finite(y))
			throw "Viewport pan must be finite";
		if (x == panX && y == panY)
			return false;
		panX = x;
		panY = y;
		revision++;
		return true;
	}

	/** Moves the world under the pointer by a screen-space delta. */
	public function panByScreen(deltaX:Float, deltaY:Float):Bool
		return setPan(panX - deltaX / zoom, panY - deltaY / zoom);

	/** Keeps the world point under the given viewport point fixed while zooming. */
	public function zoomAt(factor:Float, viewportX:Float, viewportY:Float):Bool {
		if (!finite(factor) || factor <= 0.0 || !finite(viewportX) || !finite(viewportY))
			throw "Viewport zoom gesture values must be finite and positive";
		var worldX = panX + viewportX / zoom;
		var worldY = panY + viewportY / zoom;
		var nextZoom = clamp(zoom * factor, minZoom, maxZoom);
		if (nextZoom == zoom)
			return false;
		zoom = nextZoom;
		panX = worldX - viewportX / zoom;
		panY = worldY - viewportY / zoom;
		revision++;
		return true;
	}

	/** Fits a content rectangle into a viewport, preserving its center. */
	public function fit(contentWidth:Float, contentHeight:Float, viewportWidth:Float,
			viewportHeight:Float, margin:Float = 16.0):Bool {
		if (!finite(contentWidth) || !finite(contentHeight) || !finite(viewportWidth) ||
			!finite(viewportHeight) || contentWidth <= 0.0 || contentHeight <= 0.0 ||
			viewportWidth <= 0.0 || viewportHeight <= 0.0 || margin < 0.0)
			throw "Viewport fit dimensions must be finite and positive";
		var availableWidth = Math.max(1.0, viewportWidth - margin * 2.0);
		var availableHeight = Math.max(1.0, viewportHeight - margin * 2.0);
		var nextZoom = clamp(Math.min(availableWidth / contentWidth,
			availableHeight / contentHeight), minZoom, maxZoom);
		var worldWidth = viewportWidth / nextZoom;
		var worldHeight = viewportHeight / nextZoom;
		var nextPanX = (contentWidth - worldWidth) * 0.5;
		var nextPanY = (contentHeight - worldHeight) * 0.5;
		if (nextZoom == zoom && nextPanX == panX && nextPanY == panY)
			return false;
		zoom = nextZoom;
		panX = nextPanX;
		panY = nextPanY;
		revision++;
		return true;
	}

	public function worldToViewport(x:Float, y:Float):Point
		return new Point((x - panX) * zoom, (y - panY) * zoom);

	public function viewportToWorld(x:Float, y:Float):Point
		return new Point(panX + x / zoom, panY + y / zoom);

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return Math.max(minimum, Math.min(maximum, value));

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
