import NativeKitUI;
import CompositeMode;
import LineCap;
import LineJoin;

/** Stateful, typed immediate-mode graphics encoder. */
class Canvas {
	final commands:CanvasCommandBuffer;
	var state:CanvasState;
	var saved:Array<CanvasState>;
	var openLayers:Int;

	public function new(capacity:Int = 4096) {
		commands = new CanvasCommandBuffer(capacity);
		state = new CanvasState();
		saved = [];
		openLayers = 0;
	}

	public function reset():Void {
		commands.reset();
		state = new CanvasState();
		saved = [];
		openLayers = 0;
	}

	/** Saves transform, alpha, paint, composite, and clip state. */
	public function save():Void {
		saved.push(state.copy());
		commands.save();
	}

	/** Restores the most recently saved state. */
	public function restore():Void {
		if (saved.length == 0)
			throw "Canvas restore without a matching save";
		commands.restore();
		state = saved.pop();
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
		state.transform = transform;
		commands.transform(transform.a, transform.b, transform.c, transform.d, transform.tx, transform.ty);
	}

	public function resetTransform():Void
		setTransform(Transform2D.identity());

	public function translate(x:Float, y:Float):Void
		setTransform(state.transform.translated(x, y));

	public function scale(x:Float, y:Float):Void
		setTransform(state.transform.scaled(x, y));

	public function rotate(radians:Float):Void
		setTransform(state.transform.rotated(radians));

	public function skew(xRadians:Float, yRadians:Float):Void
		setTransform(state.transform.skewed(xRadians, yRadians));

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

	public function drawImage(image:Image, rect:Rect):Void
		commands.drawImage(image, rect.x, rect.y, rect.width, rect.height);

	public function drawText(layout:TextLayout, x:Float, y:Float):Void
		commands.drawText(layout, x, y);

	public function withLayer(opacity:Float, action:Canvas->Void, mode:CompositeMode = CompositeMode.SourceOver):Void {
		beginLayer(opacity, mode);
		try {
			action(this);
		} catch (error:Dynamic) {
			endLayer();
			throw error;
		}
		endLayer();
	}

	public function beginLayer(opacity:Float, mode:CompositeMode = CompositeMode.SourceOver):Void {
		if (opacity < 0.0 || opacity > 1.0)
			throw "Canvas layer opacity must be in the range 0..1";
		commands.beginLayer(opacity, mode);
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
		UiResult.check(commands.submit(list), "displayList.update");
	}
}

private class CanvasState {
	public var transform:Transform2D;
	public var alpha:Float;
	public var paint:Null<Paint>;
	public var composite:CompositeMode;
	public var clip:Null<Rect>;

	public function new() {
		transform = Transform2D.identity();
		alpha = 1.0;
		paint = null;
		composite = CompositeMode.SourceOver;
		clip = null;
	}

	public function copy():CanvasState {
		var result = new CanvasState();
		result.transform = transform;
		result.alpha = alpha;
		result.paint = paint;
		result.composite = composite;
		result.clip = clip;
		return result;
	}
}
