package nativekit.ui.widgets;

import LayoutAlignmentY;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CommandContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.PropertyDescriptor;
import nativekit.ui.core.PropertyBinding;
import nativekit.ui.core.PropertyEditResult;
import nativekit.ui.core.PropertyEditorRegistry;
import nativekit.ui.core.PropertyType;
import nativekit.ui.core.PropertyValue;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Descriptor-driven inspector that routes edits through the active document. */
class PropertyEditor implements View {
	public final key:String;
	public final descriptors:Array<PropertyDescriptor>;
	public final style:LayoutStyle;
	public final registry:PropertyEditorRegistry;
	public var enabled:Bool;
	public var labelWidth:Float;
	final drafts:Map<String, String>;
	final errors:Map<String, String>;

	public function new(key:String, descriptors:Array<PropertyDescriptor>, ?style:LayoutStyle,
		?registry:PropertyEditorRegistry) {
		if (key == null || key.length == 0)
			throw "Property editors require a stable key";
		this.key = key;
		this.descriptors = descriptors == null ? [] : descriptors.copy();
		var ids:Map<String, Bool> = new Map();
		for (descriptor in this.descriptors) {
			if (descriptor == null || ids.exists(descriptor.id))
				throw "Property descriptor IDs must be unique";
			ids.set(descriptor.id, true);
		}
		this.style = style == null ? defaultStyle() : style.copy();
		this.registry = registry == null ? new PropertyEditorRegistry() : registry;
		enabled = true;
		labelWidth = 140.0;
		drafts = new Map();
		errors = new Map();
	}

	/** Applies a validated property value as one undoable document operation. */
	public function applyValue(context:BuildContext, descriptor:PropertyDescriptor,
			next:PropertyValue, ?coalesceKey:String):Bool {
		if (!enabled || context == null || descriptor == null)
			return false;
		var commandContext = context.commandContext == null ? new CommandContext() : context.commandContext;
		var result = new PropertyBinding(descriptor, commandContext, registry).apply(next, coalesceKey);
		switch (result) {
			case PropertyEditResult.Rejected(message):
				errors.set(descriptor.id, message);
				context.commands.refresh();
				return false;
			case PropertyEditResult.Unchanged:
				errors.remove(descriptor.id);
				return false;
			case PropertyEditResult.Applied:
				drafts.remove(descriptor.id);
				errors.remove(descriptor.id);
				context.commands.refresh();
				return true;
		}
	}

	public function build(context:BuildContext):RenderNode {
		var children:Array<KeyedView> = [];
		var categoryKeys:Map<String, Bool> = new Map();
		for (descriptor in descriptors) {
			var category = descriptor.category;
			if (category.length > 0 && !categoryKeys.exists(category)) {
				categoryKeys.set(category, true);
				children.push(new KeyedView("category:" + category,
					new Text(category)));
			}
			children.push(new KeyedView("property:" + descriptor.id,
				new PropertyEditorRow(this, descriptor)));
		}
		return new Column(key, children, style).build(context);
	}

	function setDraft(descriptor:PropertyDescriptor, value:String):Void
		drafts.set(descriptor.id, value == null ? "" : value);

	function draftOrValue(descriptor:PropertyDescriptor, value:PropertyValue):String
		return drafts.exists(descriptor.id) ? drafts.get(descriptor.id) :
			registry.editableText(value);

	function commitText(context:BuildContext, descriptor:PropertyDescriptor, text:String):Void {
		var parsed = registry.parse(descriptor.type, text);
		if (parsed == null) {
			errors.set(descriptor.id, "Value is not valid");
			context.commands.refresh();
			return;
		}
		applyValue(context, descriptor, parsed);
	}

	function errorFor(id:String):Null<String>
		return errors.get(id);

	function editorView(context:BuildContext, descriptor:PropertyDescriptor,
			value:PropertyValue):View {
		var editorKey = "editor:" + descriptor.id;
		var writable = enabled && !descriptor.readOnly;
		if (isMixed(value)) {
			if (descriptor.type == PropertyType.Bool || descriptor.type == PropertyType.Enum)
				return mixedChoice(context, descriptor, editorKey, writable);
			return textField(context, descriptor, value, editorKey, writable);
		}
		var result:View = null;
		switch (descriptor.type) {
			case PropertyType.Bool:
				var checked = false;
				switch (value) {
					case Bool(data): checked = data;
					default: checked = false;
				}
				var checkbox = new Checkbox(editorKey, "", checked, function(next) {
					applyValue(context, descriptor, PropertyValue.Bool(next));
				});
				checkbox.enabled = writable;
				result = checkbox;
			case PropertyType.Enum:
				var selected = descriptor.options.length == 0 ? "" : descriptor.options[0].key;
				switch (value) {
					case Enum(data): selected = data;
					default: selected = selected;
				}
				var enumOptions:Array<SelectOption<String>> = [];
				for (option in descriptor.options)
					enumOptions.push(new SelectOption<String>(option.key, option.label,
						option.key, option.enabled));
				var select = new Select<String>(editorKey, enumOptions, selected, function(next) {
					applyValue(context, descriptor, PropertyValue.Enum(next));
				});
				select.enabled = writable;
				result = select;
			case PropertyType.Int | PropertyType.Float:
				result = numericEditor(context, descriptor, value, editorKey, writable);
			case PropertyType.Text:
				result = textField(context, descriptor, value, editorKey, writable);
			case PropertyType.Custom(typeId):
				var extension = registry.get(typeId);
				result = extension == null ? new Text("No editor for " + typeId) :
					extension.build(editorKey, descriptor, value, context, writable,
						function(next) { applyValue(context, descriptor, next); });
		}
		return result;
	}

	function mixedChoice(context:BuildContext, descriptor:PropertyDescriptor,
			editorKey:String, writable:Bool):View {
		var choices:Array<SelectOption<String>> = [];
		if (descriptor.type == PropertyType.Bool) {
			choices.push(new SelectOption<String>("__mixed__", "Mixed", "__mixed__"));
			choices.push(new SelectOption<String>("false", "false", "false"));
			choices.push(new SelectOption<String>("true", "true", "true"));
		} else {
			choices.push(new SelectOption<String>("__mixed__", "Mixed", "__mixed__"));
			for (option in descriptor.options)
				choices.push(new SelectOption<String>(option.key, option.label,
					option.key, option.enabled));
		}
		var select = new Select<String>(editorKey, choices, "__mixed__", function(next) {
			if (next == "__mixed__")
				return;
			if (descriptor.type == PropertyType.Bool)
				applyValue(context, descriptor, PropertyValue.Bool(next == "true"));
			else
				applyValue(context, descriptor, PropertyValue.Enum(next));
		});
		select.enabled = writable;
		return select;
	}

	function numericEditor(context:BuildContext, descriptor:PropertyDescriptor,
			value:PropertyValue, editorKey:String, writable:Bool):View {
		var number:Null<Float> = null;
		switch (value) {
			case PropertyValue.Int(data): number = data;
			case PropertyValue.Float(data): number = data;
			default: number = null;
		}
		if (number == null || descriptor.minimum == null || descriptor.maximum == null)
			return textField(context, descriptor, value, editorKey, writable);
		var sliderStyle = new LayoutStyle();
		sliderStyle.width = LayoutAxis.grow();
		var step = descriptor.step == null ? 0.01 : descriptor.step;
		var slider = new Slider(editorKey + ":slider", descriptor.label, number,
			descriptor.minimum, descriptor.maximum, step, function(next) {
				if (descriptor.type == PropertyType.Int)
					applyValue(context, descriptor, PropertyValue.Int(Std.int(next)));
				else
					applyValue(context, descriptor, PropertyValue.Float(next));
			}, sliderStyle);
		slider.enabled = writable;
		var rowStyle = new LayoutStyle();
		rowStyle.width = LayoutAxis.grow();
		rowStyle.direction = LayoutDirection.LeftToRight;
		rowStyle.childAlignY = LayoutAlignmentY.Center;
		rowStyle.childGap = 6.0;
		return new Row(editorKey + ":numeric", [
			new KeyedView("slider", slider),
			new KeyedView("text", textField(context, descriptor, value,
				editorKey + ":text", writable))
		], rowStyle);
	}

	function textField(context:BuildContext, descriptor:PropertyDescriptor,
			value:PropertyValue, editorKey:String, writable:Bool):View {
		var fieldStyle = new LayoutStyle();
		fieldStyle.width = LayoutAxis.fixed(180.0);
		var field = new TextField(editorKey, draftOrValue(descriptor, value), function(next) {
			setDraft(descriptor, next);
		}, fieldStyle);
		field.enabled = writable;
		field.placeholder = isMixed(value) ? "Mixed" : null;
		field.onSubmit = function(next) {
			commitText(context, descriptor, next);
		};
		return field;
	}

	static function isMixed(value:PropertyValue):Bool {
		var result = false;
		switch (value) {
			case Mixed: result = true;
			default: result = false;
		}
		return result;
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fit();
		result.direction = LayoutDirection.TopToBottom;
		result.childGap = 6.0;
		return result;
	}
}

private class PropertyEditorRow implements View {
	final owner:PropertyEditor;
	final descriptor:PropertyDescriptor;

	public function new(owner:PropertyEditor, descriptor:PropertyDescriptor) {
		this.owner = owner;
		this.descriptor = descriptor;
	}

	public function build(context:BuildContext):RenderNode {
		var value = descriptor.readValue(context.commandContext);
		var labelStyle = new LayoutStyle();
		labelStyle.width = LayoutAxis.fixed(owner.labelWidth);
		var rowStyle = new LayoutStyle();
		rowStyle.width = LayoutAxis.grow();
		rowStyle.height = LayoutAxis.fit();
		rowStyle.direction = LayoutDirection.LeftToRight;
		rowStyle.childAlignY = LayoutAlignmentY.Center;
		rowStyle.childGap = 8.0;
		var children:Array<KeyedView> = [
			new KeyedView("label", new Text(descriptor.unit == null ? descriptor.label :
				descriptor.label + " (" + descriptor.unit + ")", labelStyle)),
			new KeyedView("value", owner.editorView(context, descriptor, value))
		];
		if (descriptor.defaultValue != null && !descriptor.readOnly) {
			var reset = new Button("Reset", null, function() {
				owner.applyValue(context, descriptor, descriptor.defaultValue);
			}, "reset");
			reset.enabled = owner.enabled;
			children.push(new KeyedView("reset", reset));
		}
		var row = new Row("row", children, rowStyle);
		var error = owner.errorFor(descriptor.id);
		if (error == null)
			return row.build(context);
		var errorStyle = new LayoutStyle();
		errorStyle.width = LayoutAxis.grow();
		var columnStyle = new LayoutStyle();
		columnStyle.width = LayoutAxis.grow();
		columnStyle.height = LayoutAxis.fit();
		columnStyle.childGap = 2.0;
		return new Column("error-row", [
			new KeyedView("field", row),
			new KeyedView("message", new Text(error, errorStyle))
		], columnStyle).build(context);
	}
}
