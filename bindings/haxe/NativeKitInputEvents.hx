import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitInputEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case NativeKitConstants.NK_EVENT_KEY: size(c,16); var v:nk_key_event=c.data; Key(c.source,v.get_key(),v.get_scancode(),v.get_action(),v.get_modifiers());
		case NativeKitConstants.NK_EVENT_TEXT_INPUT: size(c,8); var v:nk_text_input_event=c.data; TextInput(c.source,v.get_codepoint());
		case NativeKitConstants.NK_EVENT_POINTER_MOVE: size(c,16); var v:nk_pointer_move_event=c.data; PointerMove(c.source,v.get_x(),v.get_y());
		case NativeKitConstants.NK_EVENT_POINTER_BUTTON: size(c,32); var v:nk_pointer_button_event=c.data; PointerButton(c.source,v.get_button(),v.get_action(),v.get_modifiers(),v.get_x(),v.get_y());
		case NativeKitConstants.NK_EVENT_POINTER_SCROLL: size(c,16); var v:nk_pointer_scroll_event=c.data; PointerScroll(c.source,v.get_x(),v.get_y());
		case NativeKitConstants.NK_EVENT_POINTER_ENTER: PointerEnter(c.source,c.flags!=0);
		case NativeKitConstants.NK_EVENT_TOUCH: size(c,48); var v:nk_touch_event=c.data; Touch(c.source,v.get_pointer_id(),v.get_action(),v.get_tool(),v.get_modifiers(),v.get_x(),v.get_y(),v.get_pressure(),v.get_tilt_x(),v.get_tilt_y());
		case NativeKitConstants.NK_EVENT_JOYSTICK_AXIS: size(c,8); var v:nk_joystick_axis_event=c.data; JoystickAxis(c.source,v.get_axis(),v.get_value());
		case NativeKitConstants.NK_EVENT_JOYSTICK_BUTTON: size(c,8); var v:nk_joystick_button_event=c.data; JoystickButton(c.source,v.get_button(),v.get_pressed()!=0);
		case NativeKitConstants.NK_EVENT_JOYSTICK_HAT: size(c,8); var v:nk_joystick_hat_event=c.data; JoystickHat(c.source,v.get_hat(),v.get_value());
		case NativeKitConstants.NK_EVENT_GAMEPAD_AXIS: size(c,8); var v:nk_gamepad_axis_event=c.data; GamepadAxis(c.source,v.get_axis(),v.get_value());
		case NativeKitConstants.NK_EVENT_GAMEPAD_BUTTON: size(c,8); var v:nk_gamepad_button_event=c.data; GamepadButton(c.source,v.get_button(),v.get_pressed()!=0);
		default: null;
	}
	static function size(c:NativeKitEventContext,n:Int):Void if(c.data.length!=n) throw "NativeKit input event payload has an invalid size";
}
