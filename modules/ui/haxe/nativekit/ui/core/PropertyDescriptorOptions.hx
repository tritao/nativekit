package nativekit.ui.core;

typedef PropertyReader = CommandContext->PropertyValue;
typedef PropertyWriter = CommandContext->PropertyValue->Void;
typedef PropertyValidator = CommandContext->PropertyValue->Null<String>;

/** Optional presentation and validation policy for one editable property. */
class PropertyDescriptorOptions {
	public var category:String;
	public var readOnly:Bool;
	public var minimum:Null<Float>;
	public var maximum:Null<Float>;
	public var step:Null<Float>;
	public var unit:Null<String>;
	public var defaultValue:Null<PropertyValue>;
	public var options:Array<PropertyOption>;
	public var validator:Null<PropertyValidator>;

	public function new() {
		category = "";
		readOnly = false;
		minimum = null;
		maximum = null;
		step = null;
		unit = null;
		defaultValue = null;
		options = [];
		validator = null;
	}
}
