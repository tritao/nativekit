package nativekit.ui.core;

/** Stable property schema plus model read/write/validation hooks. */
class PropertyDescriptor {
	public final id:String;
	public final label:String;
	public final type:PropertyType;
	public final category:String;
	public final readOnly:Bool;
	public final minimum:Null<Float>;
	public final maximum:Null<Float>;
	public final step:Null<Float>;
	public final unit:Null<String>;
	public final defaultValue:Null<PropertyValue>;
	public final options:Array<PropertyOption>;
	public final read:PropertyReader;
	public final write:PropertyWriter;
	final validator:Null<PropertyValidator>;

	public function new(id:String, label:String, type:PropertyType,
			read:PropertyReader, write:PropertyWriter, ?settings:PropertyDescriptorOptions) {
		if (id == null || id.length == 0 || label == null || label.length == 0 ||
			read == null || write == null)
			throw "Property descriptors require an ID, label, reader, and writer";
		var config = settings == null ? new PropertyDescriptorOptions() : settings;
		if (config.minimum != null && config.maximum != null && config.minimum > config.maximum)
			throw "Property descriptor range is invalid";
		if (config.step != null && config.step <= 0.0)
			throw "Property descriptor step must be positive";
		if (type == PropertyType.Enum && (config.options == null || config.options.length == 0))
			throw "Enum properties require options";
		this.id = id;
		this.label = label;
		this.type = type;
		this.category = config.category == null ? "" : config.category;
		this.readOnly = config.readOnly;
		this.minimum = config.minimum;
		this.maximum = config.maximum;
		this.step = config.step;
		this.unit = config.unit;
		this.defaultValue = config.defaultValue;
		this.options = config.options == null ? [] : config.options.copy();
		this.read = read;
		this.write = write;
		this.validator = config.validator;
		validateOptions();
	}

	public function readValue(context:CommandContext):PropertyValue
		return read(context == null ? new CommandContext() : context);

	public function validateValue(context:CommandContext, value:PropertyValue):Null<String> {
		if (value == null)
			return "A property value is required";
		if (readOnly)
			return "Property is read-only";
		var typeError = validateType(value);
		if (typeError != null)
			return typeError;
		var number:Null<Float> = switch (value) {
			case Int(data): data;
			case Float(data): data;
			default: null;
		};
		if (number != null) {
			if (minimum != null && number < minimum)
				return "Value is below the minimum";
			if (maximum != null && number > maximum)
				return "Value is above the maximum";
		}
		return validator == null ? null : validator(context == null ? new CommandContext() : context, value);
	}

	public function option(key:String):Null<PropertyOption> {
		for (item in options)
			if (item.key == key)
				return item;
		return null;
	}

	function validateType(value:PropertyValue):Null<String> {
		var valid = false;
		switch (type) {
			case PropertyType.Bool:
				switch (value) {
					case Bool(_): valid = true;
					default:
				}
			case PropertyType.Int:
				switch (value) {
					case Int(_): valid = true;
					default:
				}
			case PropertyType.Float:
				switch (value) {
					case Float(_) | Int(_): valid = true;
					default:
				}
			case PropertyType.Text:
				switch (value) {
					case Text(_): valid = true;
					default:
				}
			case PropertyType.Enum:
				switch (value) {
					case Enum(data): return option(data) == null ? "Unknown enum option" : null;
					default:
				}
		}
		return valid ? null : "Property value type does not match its descriptor";
	}

	function validateOptions():Void {
		var keys:Map<String, Bool> = new Map();
		for (item in options) {
			if (item == null || keys.exists(item.key))
				throw "Property option keys must be unique";
			keys.set(item.key, true);
		}
	}
}
