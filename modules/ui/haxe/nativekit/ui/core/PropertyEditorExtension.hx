package nativekit.ui.core;

/**
 * Application-owned property type integration.
 *
 * Extensions keep app model types out of the core property enum while still
 * giving the inspector one consistent read/validate/write path. The editor
 * callback must be used for mutations so the owning PropertyEditor can create
 * an undoable document operation.
 */
interface PropertyEditorExtension {
	/** Stable ID used by PropertyType.Custom and PropertyValue.Custom. */
	function typeId():String;

	/** Equality for the extension's value payload. */
	function same(first:Dynamic, second:Dynamic):Bool;

	/** Human-readable value for labels, summaries, and accessibility. */
	function display(value:Dynamic):String;

	/** Single-line value used by text-based custom controls. */
	function editableText(value:Dynamic):String;

	/** Optional text import used by text-based custom controls. */
	function parse(text:String):Null<Dynamic>;

	/** Additional model-level validation beyond descriptor type checking. */
	function validate(context:CommandContext, descriptor:PropertyDescriptor,
		value:Dynamic):Null<String>;

	/**
	 * Builds the custom inspector control. `value` may be Mixed for a
	 * multi-selection. Call `apply` with a PropertyValue.Custom of this
	 * extension's type to commit an edit.
	 */
	function build(key:String, descriptor:PropertyDescriptor, value:PropertyValue,
		context:BuildContext, enabled:Bool, apply:PropertyValue->Void):View;
}
