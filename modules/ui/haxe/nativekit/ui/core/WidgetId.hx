package nativekit.ui.core;

/** Stable 31-bit identity used to reconnect ephemeral widgets across frames. */
class WidgetId {
	public final value:Int;

	public function new(value:Int) {
		if (value <= 0)
			throw "Widget IDs must be positive 31-bit values";
		this.value = value;
	}

	public inline function equals(other:WidgetId):Bool
		return other != null && value == other.value;

	public function toString():String
		return Std.string(value);
}
