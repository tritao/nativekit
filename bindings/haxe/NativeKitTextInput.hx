import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKit.Nk_result;
import NativeKit.Nk_text_input_action;
import NativeKit.Nk_text_input_type;

/** Builds and submits owned text-input state for custom native surfaces. */
class NativeKitTextInput {
	/** Creates a complete IME state. Omitted composition positions mean no active composition. */
	public static function state(text:String, textStart:Int, documentLength:Int,
			selectionStart:Int, selectionEnd:Int, ?compositionStart:Int, ?compositionEnd:Int,
			?inputType:Int, ?flags:Int, ?action:Int, ?cursorX:Float, ?cursorY:Float,
			?cursorWidth:Float, ?cursorHeight:Float):nk_text_input_state {
		var value = new nk_text_input_state();
		value.set_struct_size(nk_text_input_state.size());
		value.set_text(text);
		value.set_text_start(textStart);
		value.set_document_length(documentLength);
		value.set_selection_start(selectionStart);
		value.set_selection_end(selectionEnd);
		value.set_composition_start(compositionStart == null ? NativeKitConstants.NK_TEXT_POSITION_NONE : compositionStart);
		value.set_composition_end(compositionEnd == null ? NativeKitConstants.NK_TEXT_POSITION_NONE : compositionEnd);
		value.set_input_type(inputType == null ? Nk_text_input_type.NK_TEXT_INPUT_TEXT : inputType);
		value.set_flags(flags == null ? 0 : flags);
		value.set_action(action == null ? Nk_text_input_action.NK_TEXT_INPUT_ACTION_DEFAULT : action);
		value.set_cursor_x(cursorX == null ? 0.0 : cursorX);
		value.set_cursor_y(cursorY == null ? 0.0 : cursorY);
		value.set_cursor_width(cursorWidth == null ? 0.0 : cursorWidth);
		value.set_cursor_height(cursorHeight == null ? 0.0 : cursorHeight);
		return value;
	}

	/** Synchronizes the platform IME with the editor's current state. */
	public static function update(surface:Int, text:String, textStart:Int, documentLength:Int,
			selectionStart:Int, selectionEnd:Int, ?compositionStart:Int, ?compositionEnd:Int,
			?inputType:Int, ?flags:Int, ?action:Int, ?cursorX:Float, ?cursorY:Float,
			?cursorWidth:Float, ?cursorHeight:Float):Void {
		var result = NativeKit.nk_surface_set_text_input_state(surface,
			state(text, textStart, documentLength, selectionStart, selectionEnd,
				compositionStart, compositionEnd, inputType, flags, action,
				cursorX, cursorY, cursorWidth, cursorHeight));
		if (result != Nk_result.NK_OK)
			throw 'NativeKit text-input update failed: $result';
	}

	/** Shows or hides the platform text-input UI for a custom surface. */
	public static function setActive(surface:Int, active:Bool):Void {
		var result = NativeKit.nk_surface_set_text_input_active(surface, active ? 1 : 0);
		if (result != Nk_result.NK_OK)
			throw 'NativeKit text-input activation failed: $result';
	}
}
