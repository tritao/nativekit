package nativekit.ui.core;

/**
 * Framework-owned bridge between a descriptor and an editor document.
 *
 * Bindings keep validation, extension dispatch, and reversible mutation in one
 * place. Application code owns the descriptor callbacks and command context;
 * widgets only decide when to call apply().
 */
class PropertyBinding {
	public final descriptor:PropertyDescriptor;
	public final context:CommandContext;
	public final registry:PropertyEditorRegistry;

	public function new(descriptor:PropertyDescriptor, context:CommandContext,
			?registry:PropertyEditorRegistry) {
		if (descriptor == null)
			throw "Property bindings require a descriptor";
		this.descriptor = descriptor;
		this.context = context == null ? new CommandContext() : context;
		this.registry = registry == null ? new PropertyEditorRegistry() : registry;
	}

	public function read():PropertyValue
		return descriptor.readValue(context);

	/** Returns descriptor and extension validation without mutating the model. */
	public function validate(value:PropertyValue):Null<String> {
		var descriptorError = descriptor.validateValue(context, value);
		return descriptorError != null ? descriptorError : registry.validate(context, descriptor, value);
	}

	/**
	 * Applies one value through the active document. The optional coalescing key
	 * is intended for continuous drags or slider edits.
	 */
	public function apply(value:PropertyValue, ?coalesceKey:String):PropertyEditResult {
		var validation = validate(value);
		if (validation != null)
			return PropertyEditResult.Rejected(validation);

		if (context.document == null)
			return PropertyEditResult.Rejected("Property editing requires an active document");

		var before = read();
		if (registry.same(before, value))
			return PropertyEditResult.Unchanged;

		var latest = value;
		var key = coalesceKey == null ? "property:" + descriptor.id : coalesceKey;
		var operation = new EditOperation("Set " + descriptor.label,
			function() descriptor.write(context, latest),
			function() descriptor.write(context, before), key,
			function(nextOperation) {
				if (nextOperation == null || nextOperation.mergeData == null)
					return false;
				latest = cast nextOperation.mergeData;
				return latest != null;
			}, value);
		try {
			context.document.apply(operation, key);
		} catch (error:Dynamic) {
			return PropertyEditResult.Rejected(error == null ? "Property edit failed" : Std.string(error));
		}
		return PropertyEditResult.Applied;
	}
}
