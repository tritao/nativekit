import haxe.io.Bytes;
import NativeKitUI;
import CompositeMode;
import LineCap;
import LineJoin;
import nativekit.ui.style.BlurEffect;
import nativekit.ui.style.DropShadowEffect;
import nativekit.ui.style.EffectChain;
import nativekit.ui.style.EffectKind;
import nativekit.ui.style.Mask;
import nativekit.ui.style.MaskKind;

@:noCompletion
class CanvasCommandBuffer {
	var bytes:Bytes;
	var length:Int;

	public function new(capacity:Int) {
		if (capacity <= 0)
			throw "command buffer capacity must be positive";
		bytes = Bytes.alloc(capacity);
		length = 0;
	}

	@:noCompletion
	@:allow(Canvas)
	private function reset():Void
		length = 0;

	public function save():Void
		header(NativeKitUI.CommandOpcode.PushState, 8);

	public function restore():Void
		header(NativeKitUI.CommandOpcode.PopState, 8);

	public function transform(a:Float, b:Float, c:Float, d:Float, x:Float, y:Float):Void {
		header(NativeKitUI.CommandOpcode.SetTransform, 32);
		float(a); float(b); float(c); float(d); float(x); float(y);
	}

	public function globalAlpha(alpha:Float):Void {
		header(NativeKitUI.CommandOpcode.SetGlobalAlpha, 12);
		float(alpha);
	}

	public function composite(mode:CompositeMode):Void {
		header(NativeKitUI.CommandOpcode.SetCompositeMode, 12);
		word(cast mode);
	}

	public function paint(value:Paint):Void
		resource(NativeKitUI.CommandOpcode.SetPaint, value);

	public function clipRect(x:Float, y:Float, width:Float, height:Float):Void {
		header(NativeKitUI.CommandOpcode.ClipRect, 24);
		float(x); float(y); float(width); float(height);
	}

	public function drawPath(path:Path):Void
		resource(NativeKitUI.CommandOpcode.DrawPath, path);

	public function drawImage(image:Image, x:Float, y:Float, width:Float,
		height:Float):Void
		drawRect(NativeKitUI.CommandOpcode.DrawImage, image, x, y, width, height);

	public function drawSurface(surface:GraphicsSurface, x:Float, y:Float, width:Float,
		height:Float):Void
		drawRect(NativeKitUI.CommandOpcode.DrawRenderTarget, surface, x, y, width, height);

	public function drawText(layout:TextLayout, x:Float, y:Float):Void
		drawRect(NativeKitUI.CommandOpcode.DrawTextLayout, layout, x, y, 0.0, 0.0);

	public function beginLayer(opacity:Float, mode:CompositeMode = CompositeMode.SourceOver,
			?bounds:Rect, ?effects:EffectChain, ?mask:Mask):Void {
		var effectValue = effects == null ? EffectChain.empty() : effects;
		var hasEffects = effects != null && effectValue.effects.length > 0;
		var hasMask = mask != null;
		var blurOnly = hasEffects && effectValue.effects.length == 1 &&
			effectValue.effects[0].kind == EffectKind.Blur;
		var dropShadowOnly = hasEffects && effectValue.effects.length == 1 &&
			effectValue.effects[0].kind == EffectKind.DropShadow;
		var matrix:Array<Float> = null;
		if (hasEffects) {
			if (blurOnly) {
				matrix = [];
				for (index in 0...20)
					matrix.push(0.0);
				var blur:BlurEffect = cast effectValue.effects[0];
				matrix[0] = blur.sigma;
			} else if (dropShadowOnly) {
				matrix = [];
				for (index in 0...20)
					matrix.push(0.0);
				var shadow:DropShadowEffect = cast effectValue.effects[0];
				matrix[0] = shadow.sigma;
				matrix[2] = shadow.offsetX;
				matrix[3] = shadow.offsetY;
				matrix[4] = shadow.color.red;
				matrix[5] = shadow.color.green;
				matrix[6] = shadow.color.blue;
				matrix[7] = shadow.color.alpha;
			} else
				matrix = effectValue.colorMatrix();
		}
		if (!hasEffects && !hasMask) {
			if (bounds == null) {
				header(NativeKitUI.CommandOpcode.BeginLayer, 16);
				float(opacity);
				word(cast mode);
				return;
			}
			header(NativeKitUI.CommandOpcode.BeginLayer, 36);
			float(opacity);
			word(cast mode);
			float(bounds.x);
			float(bounds.y);
			float(bounds.width);
			float(bounds.height);
			// NKUI_LAYER_ISOLATED | NKUI_LAYER_HAS_BOUNDS.
			word(3);
			return;
		}
		if (hasMask) {
			var maskValue:Mask = cast mask;
			header(NativeKitUI.CommandOpcode.BeginLayer, 160);
			float(opacity);
			word(cast mode);
			if (bounds == null) {
				float(0.0); float(0.0); float(0.0); float(0.0);
				word(1);
			} else {
				float(bounds.x); float(bounds.y); float(bounds.width); float(bounds.height);
				word(3);
			}
			word(hasEffects ? (dropShadowOnly ? 3 : blurOnly ? 2 : 1) : 0);
			if (hasEffects)
				for (value in matrix)
					float(value);
			else
				for (index in 0...20)
					float(0.0);
			word(cast(maskValue.kind, Int));
			word(maskValue.image == null ? 0 : maskValue.image.nativeHandle().rawValue());
			for (index in 0...8) {
				var value = 0.0;
				switch maskValue.kind {
					case MaskKind.RoundedRect | MaskKind.Circle:
						if (index == 0) value = maskValue.radius;
					case MaskKind.LinearGradient:
						value = switch index {
							case 0: maskValue.x0;
							case 1: maskValue.y0;
							case 2: maskValue.x1;
							case 3: maskValue.y1;
							case 4: maskValue.alpha0;
							case 5: maskValue.alpha1;
							default: 0.0;
						};
					default:
				}
				float(value);
			}
			return;
		}
		header(NativeKitUI.CommandOpcode.BeginLayer, 120);
		float(opacity);
		word(cast mode);
		if (bounds == null) {
			float(0.0);
			float(0.0);
			float(0.0);
			float(0.0);
			word(1); // NKUI_LAYER_ISOLATED.
		} else {
			float(bounds.x);
			float(bounds.y);
			float(bounds.width);
			float(bounds.height);
			word(3); // NKUI_LAYER_ISOLATED | NKUI_LAYER_HAS_BOUNDS.
		}
		word(dropShadowOnly ? 3 : blurOnly ? 2 : 1);
		for (value in matrix)
			float(value);
	}

	public function endLayer():Void
		header(NativeKitUI.CommandOpcode.EndLayer, 8);

	public function strokePath(path:Path, width:Float, cap:LineCap = LineCap.Butt, join:LineJoin = LineJoin.Miter,
		miterLimit:Float = 4.0):Void {
		header(NativeKitUI.CommandOpcode.StrokePath, 28);
		word(path.nativeHandle().rawValue());
		float(width);
		word(cast cap);
		word(cast join);
		float(miterLimit);
	}

	/** Internal submission bridge; public callers should use Canvas and DisplayList. */
	@:noCompletion
	@:allow(DisplayList)
	@:allow(Canvas)
	private function submit(list:nkui_display_list):Int
		return submitRange(list, 0, length);

	/** Internal range submission bridge. */
	@:noCompletion
	@:allow(DisplayList)
	@:allow(Canvas)
	private function submitRange(list:nkui_display_list, offset:Int, count:Int):Int {
		if (offset < 0 || offset > length || count < 0 || count > length - offset)
			throw "Command range is out of bounds";
		return NativeKitUI.nkui_display_list_submit_slice(list, bytes, offset, count);
	}

	function resource(opcode:NativeKitUI.CommandOpcode, value:NativeKitUIResource):Void {
		header(opcode, 12);
		word(value.nativeHandle().rawValue());
	}

	function drawRect(opcode:NativeKitUI.CommandOpcode, value:NativeKitUIResource, x:Float, y:Float, width:Float,
		height:Float):Void {
		header(opcode, 28);
		word(value.nativeHandle().rawValue());
		float(x); float(y); float(width); float(height);
	}

	function header(opcode:NativeKitUI.CommandOpcode, size:Int):Void {
		require(size);
		var rawOpcode:Int = cast opcode;
		bytes.set(length, rawOpcode & 255);
		bytes.set(length + 1, (rawOpcode >> 8) & 255);
		bytes.set(length + 2, NativeKitUIConstants.NKUI_COMMAND_VERSION);
		bytes.set(length + 3, 0);
		bytes.setInt32(length + 4, size);
		length += 8;
	}

	function word(value:Int):Void {
		bytes.setInt32(length, value);
		length += 4;
	}

	function float(value:Float):Void {
		bytes.setFloat(length, value);
		length += 4;
	}

	function require(count:Int):Void {
		if (count < 0)
			throw "negative command size";
		if (length <= bytes.length - count)
			return;
		var capacity = bytes.length;
		while (capacity < length + count)
			capacity *= 2;
		var grown = Bytes.alloc(capacity);
		for (index in 0...length)
			grown.set(index, bytes.get(index));
		bytes = grown;
	}
}
