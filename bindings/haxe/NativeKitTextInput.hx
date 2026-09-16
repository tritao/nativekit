import NativeKit;
import NativeKit.TextInputState;
import NativeKit.NativeKitConstants;
import NativeKit.Result;
import NativeKit.TextInputAction;
import NativeKit.TextInputType;
import NativeKit.Handle;
import NativeKitSurface;
import NativeKitWindow;

/** Builds and submits owned text-input state for custom native surfaces. */
class NativeKitTextInput {
	/** Creates a complete IME state. Omitted composition positions mean no active composition. */
	public static function state(text:String, textStart:Int, documentLength:Int,
			selectionStart:Int, selectionEnd:Int, ?compositionStart:Int, ?compositionEnd:Int,
			?inputType:TextInputType, ?flags:TextInputFlags, ?action:TextInputAction, ?cursorX:Float, ?cursorY:Float,
			?cursorWidth:Float, ?cursorHeight:Float):TextInputState {
		var value = new TextInputState();
		value.set_text(text);
		value.set_text_start(textStart);
		value.set_document_length(documentLength);
		value.set_selection_start(selectionStart);
		value.set_selection_end(selectionEnd);
		value.set_composition_start(compositionStart == null ? NativeKitConstants.NK_TEXT_POSITION_NONE : compositionStart);
		value.set_composition_end(compositionEnd == null ? NativeKitConstants.NK_TEXT_POSITION_NONE : compositionEnd);
		value.set_input_type(inputType == null ? TextInputType.Text : inputType);
		value.set_flags(flags == null ? 0 : flags);
		value.set_action(action == null ? TextInputAction.Default : action);
		value.set_cursor_x(cursorX == null ? 0.0 : cursorX);
		value.set_cursor_y(cursorY == null ? 0.0 : cursorY);
		value.set_cursor_width(cursorWidth == null ? 0.0 : cursorWidth);
		value.set_cursor_height(cursorHeight == null ? 0.0 : cursorHeight);
		return value;
	}

	/** Synchronizes the platform IME with the editor's current state. */
	public static function update(surface:NativeKitSurface, text:String, textStart:Int, documentLength:Int,
			selectionStart:Int, selectionEnd:Int, ?compositionStart:Int, ?compositionEnd:Int,
			?inputType:TextInputType, ?flags:TextInputFlags, ?action:TextInputAction, ?cursorX:Float, ?cursorY:Float,
			?cursorWidth:Float, ?cursorHeight:Float):Void {
		updateTarget(new Handle(surface.nativeHandle().rawValue()), text, textStart, documentLength,
			selectionStart, selectionEnd, compositionStart, compositionEnd, inputType, flags, action,
			cursorX, cursorY, cursorWidth, cursorHeight);
	}

	/** Synchronizes desktop IME input when the platform backend targets a window directly. */
	public static function updateWindow(window:NativeKitWindow, text:String, textStart:Int, documentLength:Int,
			selectionStart:Int, selectionEnd:Int, ?compositionStart:Int, ?compositionEnd:Int,
			?inputType:TextInputType, ?flags:TextInputFlags, ?action:TextInputAction, ?cursorX:Float, ?cursorY:Float,
			?cursorWidth:Float, ?cursorHeight:Float):Void {
		updateTarget(new Handle(window.nativeHandle().rawValue()), text, textStart, documentLength,
			selectionStart, selectionEnd, compositionStart, compositionEnd, inputType, flags, action,
			cursorX, cursorY, cursorWidth, cursorHeight);
	}

	static function updateTarget(target:Handle, text:String, textStart:Int, documentLength:Int,
			selectionStart:Int, selectionEnd:Int, ?compositionStart:Int, ?compositionEnd:Int,
			?inputType:TextInputType, ?flags:TextInputFlags, ?action:TextInputAction, ?cursorX:Float, ?cursorY:Float,
			?cursorWidth:Float, ?cursorHeight:Float):Void {
		NativeKit.nk_surface_set_text_input_state_checked(target,
			state(text, textStart, documentLength, selectionStart, selectionEnd,
				compositionStart, compositionEnd, inputType, flags, action,
				cursorX, cursorY, cursorWidth, cursorHeight));
	}

	/** Publishes packed surface-local selection and composition rectangles. */
	public static function updateGeometryResult(surface:NativeKitSurface,
			selectionStart:Int, selectionEnd:Int, compositionStart:Int, compositionEnd:Int,
			selectionRects:haxe.io.Bytes, compositionRects:haxe.io.Bytes):Result {
		return NativeKit.nk_surface_set_text_input_geometry(new Handle(surface.nativeHandle().rawValue()),
			selectionStart, selectionEnd, compositionStart, compositionEnd,
			selectionRects, compositionRects);
	}

	/** Submits IME state without throwing, for callers that handle unsupported backends. */
	public static function updateResult(surface:NativeKitSurface, text:String, textStart:Int,
			documentLength:Int, selectionStart:Int, selectionEnd:Int, ?compositionStart:Int,
			?compositionEnd:Int, ?inputType:TextInputType, ?flags:TextInputFlags,
			?action:TextInputAction, ?cursorX:Float, ?cursorY:Float, ?cursorWidth:Float,
			?cursorHeight:Float):Result {
		var target = new Handle(surface.nativeHandle().rawValue());
		return NativeKit.nk_surface_set_text_input_state(target,
			state(text, textStart, documentLength, selectionStart, selectionEnd,
				compositionStart, compositionEnd, inputType, flags, action,
				cursorX, cursorY, cursorWidth, cursorHeight));
	}

	/** Shows or hides the platform text-input UI for a custom surface. */
	public static function setActive(surface:NativeKitSurface, active:Bool):Void {
		setTargetActive(new Handle(surface.nativeHandle().rawValue()), active);
	}

	/** Shows or hides the platform text-input UI when the backend targets a window directly. */
	public static function setWindowActive(window:NativeKitWindow, active:Bool):Void {
		setTargetActive(new Handle(window.nativeHandle().rawValue()), active);
	}

	static function setTargetActive(target:Handle, active:Bool):Void {
		NativeKit.nk_surface_set_text_input_active_checked(target, active);
	}

	/** Changes platform text-input visibility without throwing on unsupported backends. */
	public static function setActiveResult(surface:NativeKitSurface, active:Bool):Result
		return NativeKit.nk_surface_set_text_input_active(
			new Handle(surface.nativeHandle().rawValue()), active);
}
