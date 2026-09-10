import NativeKit;
import NativeKit.NativeKitConstants;

/** Builds and submits owned text-input state for custom native surfaces. */
class NativeKitTextInput {
	/** Creates a complete IME state. Omitted composition positions mean no active composition. */
	public static function state(text:String, selectionStart:Int, selectionEnd:Int,
			?compositionStart:Int, ?compositionEnd:Int):nk_text_input_state {
		var value = new nk_text_input_state();
		value.set_struct_size(nk_text_input_state.size());
		value.set_text(text);
		value.set_selection_start(selectionStart);
		value.set_selection_end(selectionEnd);
		value.set_composition_start(compositionStart == null ? NativeKitConstants.NK_TEXT_POSITION_NONE : compositionStart);
		value.set_composition_end(compositionEnd == null ? NativeKitConstants.NK_TEXT_POSITION_NONE : compositionEnd);
		return value;
	}

	/** Synchronizes the platform IME with the editor's current state. */
	public static function update(surface:Int, text:String, selectionStart:Int, selectionEnd:Int,
			?compositionStart:Int, ?compositionEnd:Int):Void {
		var result = NativeKit.nk_surface_set_text_input_state(surface,
			state(text, selectionStart, selectionEnd, compositionStart, compositionEnd));
		if (result != NativeKitConstants.NK_OK)
			throw 'NativeKit text-input update failed: $result';
	}

	/** Shows or hides the platform text-input UI for a custom surface. */
	public static function setActive(surface:Int, active:Bool):Void {
		var result = NativeKit.nk_surface_set_text_input_active(surface, active ? 1 : 0);
		if (result != NativeKitConstants.NK_OK)
			throw 'NativeKit text-input activation failed: $result';
	}
}
