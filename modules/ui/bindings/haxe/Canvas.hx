import NativeKitUI;
import CompositeMode;
import LineCap;
import LineJoin;
import nativekit.ui.style.EffectChain;

/** Stateful, typed immediate-mode graphics encoder. */
class Canvas {
	final commands:CanvasCommandBuffer;
	var state:CanvasState;
	var saved:Array<CanvasState>;
	var savedDepth:Int;
	var openLayers:Int;
	final transientResources:Array<NativeKitUIResource>;

	public function new(capacity:Int = 4096) {
		commands = new CanvasCommandBuffer(capacity);
		state = new CanvasState();
		saved = [];
		savedDepth = 0;
		openLayers = 0;
		transientResources = [];
	}

	public function reset():Void {
		releaseTransientResources();
		commands.reset();
		state.reset();
		savedDepth = 0;
		openLayers = 0;
	}

	/** Saves transform, alpha, paint, composite, and clip state. */
	public function save():Void {
		if (savedDepth == saved.length)
			saved.push(new CanvasState());
		saved[savedDepth].copyFrom(state);
		savedDepth++;
		commands.save();
	}

	/** Restores the most recently saved state. */
	public function restore():Void {
		if (savedDepth == 0)
			throw "Canvas restore without a matching save";
		commands.restore();
		savedDepth--;
		state.copyFrom(saved[savedDepth]);
	}

	public function withState(action:Canvas->Void):Void {
		save();
		try {
			action(this);
		} catch (error:Dynamic) {
			restore();
			throw error;
		}
		restore();
	}

	public function setTransform(transform:Transform2D):Void {
		setTransformValues(transform.a, transform.b, transform.c, transform.d, transform.tx, transform.ty);
	}

	public function resetTransform():Void
		setTransformValues(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);

	public function translate(x:Float, y:Float):Void {
		setTransformValues(state.a, state.b, state.c, state.d,
			state.a * x + state.c * y + state.tx,
			state.b * x + state.d * y + state.ty);
	}

	public function scale(x:Float, y:Float):Void
		setTransformValues(state.a * x, state.b * x, state.c * y, state.d * y,
			state.tx, state.ty);

	public function rotate(radians:Float):Void {
		var cosine = Math.cos(radians);
		var sine = Math.sin(radians);
		setTransformValues(state.a * cosine + state.c * sine,
			state.b * cosine + state.d * sine,
			-state.a * sine + state.c * cosine,
			-state.b * sine + state.d * cosine,
			state.tx, state.ty);
	}

	public function skew(xRadians:Float, yRadians:Float):Void {
		var x = Math.tan(xRadians);
		var y = Math.tan(yRadians);
		setTransformValues(state.a + state.c * y, state.b + state.d * y,
			state.a * x + state.c, state.b * x + state.d,
			state.tx, state.ty);
	}

	function setTransformValues(a:Float, b:Float, c:Float, d:Float, tx:Float, ty:Float):Void {
		state.a = a;
		state.b = b;
		state.c = c;
		state.d = d;
		state.tx = tx;
		state.ty = ty;
		commands.transform(a, b, c, d, tx, ty);
	}

	public function setAlpha(alpha:Float):Void {
		if (alpha < 0.0 || alpha > 1.0)
			throw "Canvas alpha must be in the range 0..1";
		state.alpha = alpha;
		commands.globalAlpha(alpha);
	}

	public function setPaint(paint:Paint):Void {
		state.paint = paint;
		commands.paint(paint);
	}

	public function setComposite(mode:CompositeMode):Void {
		state.composite = mode;
		commands.composite(mode);
	}

	public function clip(rect:Rect):Void {
		state.clip = rect;
		commands.clipRect(rect.x, rect.y, rect.width, rect.height);
	}

	public function withClip(rect:Rect, action:Canvas->Void):Void
		withState(function(canvas) { canvas.clip(rect); action(canvas); });

	public function fill(path:Path, paint:Paint):Void {
		setPaint(paint);
		commands.drawPath(path);
	}

	public function stroke(path:Path, paint:Paint, width:Float, cap:LineCap = LineCap.Butt, join:LineJoin = LineJoin.Miter,
		miterLimit:Float = 4.0):Void {
		setPaint(paint);
		commands.strokePath(path, width, cap, join, miterLimit);
	}

	/** Strokes a transient path and transfers its lifetime to this canvas update. */
	public function strokeTransient(path:Path, color:Color, width:Float,
			cap:LineCap = LineCap.Round, join:LineJoin = LineJoin.Round,
			miterLimit:Float = 4.0):Void {
		if (path == null || path.isDisposed() || color == null || width <= 0.0)
			throw "Transient stroke requires a live path, color, and positive width";
		try {
			var paint = SolidPaint.create(color);
			try {
				stroke(path, paint, width, cap, join, miterLimit);
				transientResources.push(path);
				transientResources.push(paint);
			} catch (error:Dynamic) {
				paint.dispose();
				throw error;
			}
		} catch (error:Dynamic) {
			path.dispose();
			throw error;
		}
	}

	/** Adds one filled rectangle using a transient path and solid paint resource. */
	public function fillRect(rect:Rect, color:Color):Void {
		if (rect == null || color == null || rect.width <= 0.0 || rect.height <= 0.0)
			throw "Filled rectangle requires positive bounds and a color";
		var path = new PathBuilder().moveTo(rect.x, rect.y).lineTo(rect.x + rect.width, rect.y)
			.lineTo(rect.x + rect.width, rect.y + rect.height).lineTo(rect.x, rect.y + rect.height)
			.close().build();
		try {
			var paint = SolidPaint.create(color);
			try {
				fill(path, paint);
				transientResources.push(path);
				transientResources.push(paint);
			} catch (error:Dynamic) {
				paint.dispose();
				throw error;
			}
		} catch (error:Dynamic) {
			path.dispose();
			throw error;
		}
	}

	/** Adds a filled rectangle when its bounds and color are valid. */
	public function fillRectIfPositive(rect:Rect, color:Color):Bool {
		if (rect == null || color == null || rect.width <= 0.0 || rect.height <= 0.0)
			return false;
		fillRect(rect, color);
		return true;
	}

	/**
	 * Adds one gradient-filled rectangle and retains its temporary resources through update.
	 * Gradient endpoints use the same user coordinate space as `rect`.
	 */
	public function fillLinearGradientRect(rect:Rect, startX:Float, startY:Float,
			endX:Float, endY:Float, stops:Array<GradientStop>):Void {
		if (rect == null || rect.width <= 0.0 || rect.height <= 0.0)
			throw "Gradient rectangle requires positive bounds";
		var path = new PathBuilder().moveTo(rect.x, rect.y).lineTo(rect.x + rect.width, rect.y)
			.lineTo(rect.x + rect.width, rect.y + rect.height).lineTo(rect.x, rect.y + rect.height)
			.close().build();
		try {
			var paint = LinearGradientPaint.create(startX, startY, endX, endY, stops);
			try {
				fill(path, paint);
				transientResources.push(path);
				transientResources.push(paint);
			} catch (error:Dynamic) {
				paint.dispose();
				throw error;
			}
		} catch (error:Dynamic) {
			path.dispose();
			throw error;
		}
	}

	public function drawImage(image:Image, rect:Rect):Void
		commands.drawImage(image, rect.x, rect.y, rect.width, rect.height);

	/** Composites an offscreen graphics surface into this canvas. */
	public function drawSurface(surface:GraphicsSurface, rect:Rect):Void
		commands.drawSurface(surface, rect.x, rect.y, rect.width, rect.height);

	public function drawText(layout:TextLayout, x:Float, y:Float):Void
		commands.drawText(layout, x, y);

	public function withLayer(opacity:Float, action:Canvas->Void, mode:CompositeMode = CompositeMode.SourceOver,
			?bounds:Rect, ?effects:EffectChain):Void {
		beginLayer(opacity, mode, bounds, effects);
		try {
			action(this);
		} catch (error:Dynamic) {
			endLayer();
			throw error;
		}
		endLayer();
	}

	public function beginLayer(opacity:Float, mode:CompositeMode = CompositeMode.SourceOver,
			?bounds:Rect, ?effects:EffectChain):Void {
		if (opacity < 0.0 || opacity > 1.0)
			throw "Canvas layer opacity must be in the range 0..1";
		if (bounds != null && (!Math.isFinite(bounds.x) || !Math.isFinite(bounds.y) ||
			!Math.isFinite(bounds.width) || !Math.isFinite(bounds.height) ||
			bounds.width <= 0.0 || bounds.height <= 0.0))
			throw "Canvas layer bounds must be finite and positive";
		commands.beginLayer(opacity, mode, bounds, effects);
		openLayers++;
	}

	public function endLayer():Void {
		if (openLayers == 0)
			throw "Canvas endLayer without a matching beginLayer";
		commands.endLayer();
		openLayers--;
	}

	/** Commits this canvas to a retained display list. */
	public function update(displayList:DisplayList):Void
		displayList.update(this);

	@:allow(DisplayList)
	private function submitTo(list:nkui_display_list):Void {
		if (openLayers != 0)
			throw "Canvas update with an open layer";
		try {
			UiResult.check(commands.submit(list), "displayList.update");
		} catch (error:Dynamic) {
			releaseTransientResources();
			throw error;
		}
		releaseTransientResources();
	}

	function releaseTransientResources():Void {
		for (resource in transientResources)
			resource.dispose();
		transientResources.resize(0);
	}
}

private class CanvasState {
	public var a:Float;
	public var b:Float;
	public var c:Float;
	public var d:Float;
	public var tx:Float;
	public var ty:Float;
	public var alpha:Float;
	public var paint:Null<Paint>;
	public var composite:CompositeMode;
	public var clip:Null<Rect>;

	public function new() {
		a = 1.0;
		b = 0.0;
		c = 0.0;
		d = 1.0;
		tx = 0.0;
		ty = 0.0;
		alpha = 1.0;
		paint = null;
		composite = CompositeMode.SourceOver;
		clip = null;
	}

	public function reset():Void {
		a = 1.0;
		b = 0.0;
		c = 0.0;
		d = 1.0;
		tx = 0.0;
		ty = 0.0;
		alpha = 1.0;
		paint = null;
		composite = CompositeMode.SourceOver;
		clip = null;
	}

	public function copyFrom(value:CanvasState):Void {
		a = value.a;
		b = value.b;
		c = value.c;
		d = value.d;
		tx = value.tx;
		ty = value.ty;
		alpha = value.alpha;
		paint = value.paint;
		composite = value.composite;
		clip = value.clip;
	}
}
