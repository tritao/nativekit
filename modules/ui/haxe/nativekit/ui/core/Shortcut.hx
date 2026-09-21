package nativekit.ui.core;

/** A normalized key/modifier chord used by application commands. */
class Shortcut {
	public final key:Int;
	public final modifiers:Int;

	public function new(key:Int, modifiers:Int = 0) {
		if (key <= 0)
			throw "Shortcuts require a positive key";
		this.key = key;
		this.modifiers = normalizeModifiers(modifiers);
	}

	/** Ignores lock-state bits; command chords are physical modifier chords. */
	public static function normalizeModifiers(modifiers:Int):Int
		return modifiers & (UiModifier.Shift | UiModifier.Control | UiModifier.Alt |
			UiModifier.Super);

	public function matches(key:Int, modifiers:Int):Bool
		return this.key == key && this.modifiers == normalizeModifiers(modifiers);

	public function equals(other:Null<Shortcut>):Bool
		return other != null && key == other.key && modifiers == other.modifiers;

	public function toString():String
		return 'Shortcut($key, $modifiers)';
}
