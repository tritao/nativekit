import haxe.io.Bytes;
import NativeKitUI;
import CompositeMode;
import Color;
import LineCap;
import LineJoin;
import nativekit.ui.style.BlurEffect;
import nativekit.ui.style.ColorMatrixEffect;
import nativekit.ui.style.CustomEffect;
import nativekit.ui.style.DropShadowEffect;
import nativekit.ui.style.Effect;
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

	public function drawBoxShadow(x:Float, y:Float, width:Float, height:Float, offsetX:Float,
		 offsetY:Float, blurRadius:Float, spread:Float, radii:Array<Float>, color:Color):Void {
		if (radii == null || radii.length != 4 || color == null)
			throw "Box shadow requires four corner radii and a color";
		header(NativeKitUI.CommandOpcode.DrawBoxShadow, 72);
		float(x); float(y); float(width); float(height);
		float(offsetX); float(offsetY); float(blurRadius); float(spread);
		for (radius in radii)
			float(radius);
		float(color.red); float(color.green); float(color.blue); float(color.alpha);
	}

	public function drawText(layout:TextLayout, x:Float, y:Float):Void
		drawRect(NativeKitUI.CommandOpcode.DrawTextLayout, layout, x, y, 0.0, 0.0);

	public function beginLayer(opacity:Float, mode:CompositeMode = CompositeMode.SourceOver,
			?bounds:Rect, ?effects:EffectChain, ?mask:Mask, ?backdropEffects:EffectChain):Void {
		var foreground = effects == null ? [] : effects.normalized().effects;
		var backdrop = backdropEffects == null ? [] : backdropEffects.normalized().effects;
		if (foreground.length > NativeKitUIConstants.NKUI_EFFECT_PROGRAM_MAX_OPS ||
			backdrop.length > NativeKitUIConstants.NKUI_EFFECT_PROGRAM_MAX_OPS)
			throw "Effect chains exceed the native operation limit";
		var hasMask = mask != null;
		var isolated = opacity < 1.0 || bounds != null || foreground.length > 0 || backdrop.length > 0 || hasMask;
		var flags = isolated ? 1 : 0;
		if (bounds != null)
			flags |= 2;
		// Fixed prefix + only the active foreground and backdrop operations.
		var operationBytes = 4 + 20 * 4 + 4 * 4 + 4 * 4 + 20 * 4;
		var prefixBytes = 8 + 4 + 4 + 16 + 4 + 40 + 4 + 4;
		var commandSize = prefixBytes + (foreground.length + backdrop.length) * operationBytes;
		header(NativeKitUI.CommandOpcode.BeginLayer, commandSize,
			NativeKitUIConstants.NKUI_LAYER_COMMAND_VERSION);
		float(opacity);
		word(cast mode);
		if (bounds == null) {
			float(0.0); float(0.0); float(0.0); float(0.0);
		} else {
			float(bounds.x); float(bounds.y); float(bounds.width); float(bounds.height);
		}
		word(flags);
		writeMask(mask);
		word(foreground.length);
		word(backdrop.length);
		for (effect in foreground)
			writeEffect(effect);
		for (effect in backdrop)
			writeEffect(effect);
	}

	function writeEffect(value:Effect):Void {
		var matrix:Array<Float> = [];
		for (index in 0...20)
			matrix.push(0.0);
		var kind = 0;
		switch value.kind {
			case EffectKind.Blur:
				kind = 2;
				var blur:BlurEffect = cast value;
				matrix[0] = blur.sigma;
			case EffectKind.DropShadow:
				var shadow:DropShadowEffect = cast value;
				kind = 3;
				matrix[0] = shadow.sigma;
				matrix[2] = shadow.offsetX;
				matrix[3] = shadow.offsetY;
				matrix[4] = shadow.color.red;
				matrix[5] = shadow.color.green;
				matrix[6] = shadow.color.blue;
				matrix[7] = shadow.color.alpha;
			case EffectKind.Custom:
				kind = 4;
			case EffectKind.ColorMatrix:
				kind = 1;
				var colorMatrix:ColorMatrixEffect = cast value;
				matrix = colorMatrix.matrix.copy();
			default:
				throw "Effect chain normalization left an unsupported operation";
		}
		word(kind);
		for (component in matrix)
			float(component);
		if (kind == 4) {
			var custom:CustomEffect = cast value;
			word(custom.definition.id);
			word(custom.components.length);
			word(1);
			word(1);
			float(custom.definition.overflow.left);
			float(custom.definition.overflow.top);
			float(custom.definition.overflow.right);
			float(custom.definition.overflow.bottom);
			for (index in 0...NativeKitUIConstants.NKUI_CUSTOM_EFFECT_PARAMETER_COMPONENTS)
				float(index < custom.components.length ? custom.components[index] : 0.0);
		} else
			writeEmptyCustom();
	}

	function writeEmptyCustom():Void {
		for (index in 0...4)
			word(0);
		for (index in 0...4)
			float(0.0);
		for (index in 0...NativeKitUIConstants.NKUI_CUSTOM_EFFECT_PARAMETER_COMPONENTS)
			float(0.0);
	}

	function writeMask(value:Mask):Void {
		if (value == null) {
			word(0);
			word(0);
			for (index in 0...8)
				float(0.0);
			return;
		}
		word(cast(value.kind, Int));
		word(value.image == null ? 0 : value.image.nativeHandle().rawValue());
		for (index in 0...8) {
			var parameter = 0.0;
			switch value.kind {
				case MaskKind.RoundedRect | MaskKind.Circle:
					if (index == 0) parameter = value.radius;
				case MaskKind.LinearGradient:
					parameter = switch index {
						case 0: value.x0;
						case 1: value.y0;
						case 2: value.x1;
						case 3: value.y1;
						case 4: value.alpha0;
						case 5: value.alpha1;
						default: 0.0;
					};
				default:
			}
			float(parameter);
		}
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

	function header(opcode:NativeKitUI.CommandOpcode, size:Int,
		version:Int = NativeKitUIConstants.NKUI_COMMAND_VERSION):Void {
		require(size);
		var rawOpcode:Int = cast opcode;
		bytes.set(length, rawOpcode & 255);
		bytes.set(length + 1, (rawOpcode >> 8) & 255);
		bytes.set(length + 2, version);
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
