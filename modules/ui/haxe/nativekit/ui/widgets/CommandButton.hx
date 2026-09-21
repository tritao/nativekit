package nativekit.ui.widgets;

import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CommandContext;
import nativekit.ui.core.CommandRegistry;
import nativekit.ui.core.CommandResult;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Button whose label, enabled state, and action are owned by a Command. */
class CommandButton implements View {
	public final key:String;
	public final commandId:String;
	public final registry:Null<CommandRegistry>;
	public final invocationContext:Null<CommandContext>;
	public final style:Null<LayoutStyle>;
	public var variant:ButtonVariant;
	public var onResult:Null<CommandResult->Void>;

	public function new(key:String, commandId:String, ?registry:CommandRegistry,
			?invocationContext:CommandContext, ?style:LayoutStyle,
			?onResult:CommandResult->Void) {
		if (key == null || key.length == 0 || commandId == null || commandId.length == 0)
			throw "Command buttons require stable keys and command IDs";
		this.key = key;
		this.commandId = commandId;
		this.registry = registry;
		this.invocationContext = invocationContext;
		this.style = style == null ? null : style.copy();
		variant = ButtonVariant.Primary;
		this.onResult = onResult;
	}

	public function build(context:BuildContext):RenderNode {
		var commands = registry == null ? context.commands : registry;
		var command = commands.get(commandId);
		if (command == null) {
			var missing = new Button("Missing command: " + commandId, style, null, key);
			missing.enabled = false;
			return missing.build(context);
		}
		var actualContext = invocationContext == null ? context.commandContext : invocationContext;
		var button = new Button(command.label, style, function() {
			var result = commands.executeContext(commandId, actualContext);
			if (onResult != null)
				onResult(result);
		}, key);
		button.variant = variant;
		button.enabled = command.isEnabled(actualContext);
		button.selected = command.isChecked(actualContext);
		button.classes = ["command-button"];
		return button.build(context);
	}
}
