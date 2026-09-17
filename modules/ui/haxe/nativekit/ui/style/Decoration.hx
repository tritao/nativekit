package nativekit.ui.style;

import Canvas;
import ResolvedLayoutItem;

/**
 * Rendering-independent decoration value painted after layout resolves geometry.
 *
 * Decorations deliberately follow the same value-object contract as Effect:
 * built-ins override copy/equality/interpolation, while custom decorations can
 * inherit the identity/discrete defaults without any runtime type inspection.
 */
class Decoration {
	public final kind:DecorationKind;

	public function new(kind:DecorationKind = DecorationKind.Custom)
		this.kind = kind;

	/** Paints this decoration in node-local coordinates; the base value paints nothing. */
	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void
		{}

	/** Returns an independent value copy when the decoration has mutable state. */
	public function copy():Decoration
		return this;

	/** Compares the complete semantic value, not just the decoration family. */
	public function isEqual(other:Decoration):Bool
		return other == this;

	/** Interpolates compatible values; custom decorations switch discretely. */
	public function interpolate(other:Decoration, amount:Float):Decoration
		return Decoration.discrete(this, other, amount);

	public function describe():String
		return Std.string(kind);

	public function toString():String
		return describe();

	public static function discrete(left:Decoration, right:Decoration,
			amount:Float):Decoration
		return amount < 0.5 || right == null ? left.copy() : right.copy();
}
