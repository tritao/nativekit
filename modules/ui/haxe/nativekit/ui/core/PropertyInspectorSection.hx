package nativekit.ui.core;

/** Stable grouping of descriptors rendered by a shared property inspector. */
class PropertyInspectorSection {
	public final id:String;
	public final label:String;
	public final descriptors:Array<PropertyDescriptor>;
	public var expanded(default, null):Bool;

	public function new(id:String, label:String, descriptors:Array<PropertyDescriptor>,
			expanded:Bool = true) {
		if (id == null || id.length == 0 || label == null || label.length == 0 ||
			descriptors == null || descriptors.length == 0)
			throw "Inspector sections require an ID, label, and descriptors";
		this.id = id;
		this.label = label;
		this.descriptors = descriptors.copy();
		var ids:Map<String, Bool> = new Map();
		for (descriptor in this.descriptors) {
			if (descriptor == null || ids.exists(descriptor.id))
				throw "Inspector section descriptor IDs must be unique";
			ids.set(descriptor.id, true);
		}
		this.expanded = expanded;
	}

	/** Changes the section state and reports whether it changed. */
	public function setExpanded(value:Bool):Bool {
		if (expanded == value)
			return false;
		expanded = value;
		return true;
	}
}
