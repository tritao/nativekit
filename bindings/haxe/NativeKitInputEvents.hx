import NativeKit;
import NativeKit.EventKind;
import NativeKit.GamepadAxisEvent;
import NativeKit.GamepadButtonEvent;
import NativeKit.JoystickAxisEvent;
import NativeKit.JoystickButtonEvent;
import NativeKit.JoystickHatEvent;
import NativeKit.KeyEvent;
import NativeKit.PointerButtonEvent;
import NativeKit.PointerMoveEvent;
import NativeKit.PointerScrollEvent;
import NativeKit.TextEditEvent;
import NativeKit.TextInputEvent;
import NativeKit.TouchEvent;
import NativeKitEventContext;
import NativeKitEventBytes;
import NativeKitEventValue;
import NativeKitEventValue.NativeKitTextEdit;

class NativeKitInputEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case EventKind.Key: NativeKitEventBytes.requireSize(c.data,16); var v:KeyEvent=c.data; Key(c.source,v.get_key(),v.get_scancode(),v.get_action(),v.get_modifiers());
		case EventKind.TextInput: NativeKitEventBytes.requireSize(c.data,8); var v:TextInputEvent=c.data; TextInput(c.source,v.get_codepoint());
		case EventKind.TextEdit:
			NativeKitEventBytes.requireMinimumSize(c.data,48); var v:TextEditEvent=c.data;
			var text=NativeKitEventBytes.readUtf8Slice(c.data,v.get_text_offset(),v.get_text_length(),48);
			TextEdit(c.source,new NativeKitTextEdit(v.get_action(),text,v.get_replace_start(),v.get_replace_end(),v.get_selection_start(),v.get_selection_end(),v.get_composition_start(),v.get_composition_end(),v.get_selection_affinity()));
		case EventKind.TextAction:
			NativeKitEventBytes.requireSize(c.data,8); var v:TextInputActionEvent=c.data;
			TextAction(c.source,v.get_action());
		case EventKind.AccessibilityAction:
			NativeKitEventBytes.requireMinimumSize(c.data,32);
			var nodeId = NativeKitEventBytes.readU32(c.data, 0);
			var action = NativeKitEventBytes.readU32(c.data, 4);
			var valueOffset = NativeKitEventBytes.readU32(c.data, 8);
			var valueLength = NativeKitEventBytes.readU32(c.data, 12);
			var selectionStart = NativeKitEventBytes.readU32(c.data, 16);
			var selectionEnd = NativeKitEventBytes.readU32(c.data, 20);
			var granularity = NativeKitEventBytes.readU32(c.data, 24);
			if (nodeId <= 0 || action < 1 || action > 11 || NativeKitEventBytes.readU32(c.data, 28) != 0)
				throw "NativeKit accessibility action payload has invalid fields";
			var value = NativeKitEventBytes.readUtf8Slice(c.data, valueOffset, valueLength, 32);
			if (valueLength > 0 && (valueOffset + valueLength >= c.data.length ||
				c.data.get(valueOffset + valueLength) != 0))
				throw "NativeKit accessibility action value is not NUL-terminated";
			AccessibilityAction(c.source, nodeId, action, value, selectionStart,
				selectionEnd, granularity);
		case EventKind.PointerMove: NativeKitEventBytes.requireSize(c.data,16); var v:PointerMoveEvent=c.data; PointerMove(c.source,v.get_x(),v.get_y());
		case EventKind.PointerButton: NativeKitEventBytes.requireSize(c.data,32); var v:PointerButtonEvent=c.data; PointerButton(c.source,v.get_button(),v.get_action(),v.get_modifiers(),v.get_x(),v.get_y());
		case EventKind.PointerScroll: NativeKitEventBytes.requireSize(c.data,16); var v:PointerScrollEvent=c.data; PointerScroll(c.source,v.get_x(),v.get_y());
		case EventKind.PointerEnter: PointerEnter(c.source,c.flags!=0);
		case EventKind.Touch: NativeKitEventBytes.requireSize(c.data,48); var v:TouchEvent=c.data; Touch(c.source,v.get_pointer_id(),v.get_action(),v.get_tool(),v.get_modifiers(),v.get_x(),v.get_y(),v.get_pressure(),v.get_tilt_x(),v.get_tilt_y());
		case EventKind.JoystickAxis: NativeKitEventBytes.requireSize(c.data,8); var v:JoystickAxisEvent=c.data; JoystickAxis(c.source,v.get_axis(),v.get_value());
		case EventKind.JoystickButton: NativeKitEventBytes.requireSize(c.data,8); var v:JoystickButtonEvent=c.data; JoystickButton(c.source,v.get_button(),v.get_pressed());
		case EventKind.JoystickHat: NativeKitEventBytes.requireSize(c.data,8); var v:JoystickHatEvent=c.data; JoystickHat(c.source,v.get_hat(),v.get_value());
		case EventKind.GamepadAxis: NativeKitEventBytes.requireSize(c.data,8); var v:GamepadAxisEvent=c.data; GamepadAxis(c.source,v.get_axis(),v.get_value());
		case EventKind.GamepadButton: NativeKitEventBytes.requireSize(c.data,8); var v:GamepadButtonEvent=c.data; GamepadButton(c.source,v.get_button(),v.get_pressed());
		default: null;
	}
}
