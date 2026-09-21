package nativekit.ui.core;

/** Registry for application-defined property values and inspector controls. */
class PropertyEditorRegistry {
	final extensions:Map<String, PropertyEditorExtension>;

	public function new(?initial:Array<PropertyEditorExtension>) {
		extensions = new Map();
		if (initial != null)
			for (extension in initial)
				register(extension);
	}

	public function register(extension:PropertyEditorExtension):Void {
		if (extension == null || extension.typeId() == null || extension.typeId().length == 0)
			throw "Property editor extensions require a stable type ID";
		var id = extension.typeId();
		if (extensions.exists(id))
			throw 'Property editor extension already registered: $id';
		extensions.set(id, extension);
	}

	public function unregister(typeId:String):Bool
		return typeId != null && extensions.remove(typeId);

	public function get(typeId:String):Null<PropertyEditorExtension>
		return typeId == null ? null : extensions.get(typeId);

	public function validate(context:CommandContext, descriptor:PropertyDescriptor,
		value:PropertyValue):Null<String> {
		if (value == null)
			return null;
		return switch (value) {
			case Custom(typeId, data):
				var extension = get(typeId);
				if (extension == null)
					"No property editor extension is registered for " + typeId;
				else
					extension.validate(context == null ? new CommandContext() : context,
						descriptor, data);
			default: null;
		};
	}

	public function same(first:Null<PropertyValue>, second:Null<PropertyValue>):Bool {
		if (first == null || second == null)
			return first == second;
		return switch ([first, second]) {
			case [Custom(typeA, firstData), Custom(typeB, secondData)]:
				if (typeA != typeB)
					false;
				else {
					var extension = get(typeA);
					extension == null ? PropertyValueTools.same(first, second) :
						extension.same(firstData, secondData);
				}
			default: PropertyValueTools.same(first, second);
		};
	}

	public function display(value:Null<PropertyValue>, mixedText:String = "—"):String {
		if (value == null)
			return "";
		return switch (value) {
			case Custom(typeId, data):
				var extension = get(typeId);
				extension == null ? PropertyValueTools.display(value, mixedText) :
				extension.display(data);
			case Mixed: mixedText;
			default: PropertyValueTools.display(value, mixedText);
		};
	}

	public function editableText(value:Null<PropertyValue>):String {
		if (value == null)
			return "";
		return switch (value) {
			case Custom(typeId, data):
				var extension = get(typeId);
				extension == null ? PropertyValueTools.editableText(value) :
				extension.editableText(data);
			default: PropertyValueTools.editableText(value);
		};
	}

	/** Parses a custom value and restores the extension type tag. */
	public function parse(type:PropertyType, text:String):Null<PropertyValue> {
		return switch (type) {
			case Custom(typeId):
				var extension = get(typeId);
				if (extension == null)
					null;
				else {
					var data = extension.parse(text);
					data == null ? null : PropertyValue.Custom(typeId, data);
				}
			default: PropertyValueTools.parse(type, text);
		};
	}
}
