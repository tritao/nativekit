package nativekit.ui.core;

/** An application action exposed to menus, toolbars, palettes, and shortcuts. */
class Command {
	public final id:String;
	public final label:String;
	public final shortcut:Null<Shortcut>;
	final action:Void->Void;
	var contextualAction:Null<CommandContext->CommandResult>;
	final enabledPredicate:Null<Void->Bool>;
	final checkedPredicate:Null<Void->Bool>;
	var contextualEnabledPredicate:Null<CommandContext->Bool>;
	var contextualCheckedPredicate:Null<CommandContext->Bool>;

	public function new(id:String, label:String, action:Void->Void,
			?shortcut:Shortcut, ?enabled:Void->Bool, ?checked:Void->Bool) {
		if (id == null || id.length == 0 || label == null || label.length == 0 || action == null)
			throw "Commands require a stable ID, label, and action";
		this.id = id;
		this.label = label;
		this.action = action;
		this.contextualAction = null;
		this.shortcut = shortcut;
		this.enabledPredicate = enabled;
		this.checkedPredicate = checked;
		this.contextualEnabledPredicate = null;
		this.contextualCheckedPredicate = null;
	}

	/** Creates a command whose action and predicates receive invocation context. */
	public static function contextual(id:String, label:String,
			action:CommandContext->CommandResult, ?shortcut:Shortcut,
			enabled:CommandContext->Bool = null,
			checked:CommandContext->Bool = null):Command {
		if (action == null)
			throw "Contextual commands require an action";
		var result = new Command(id, label, function() {}, shortcut);
		result.contextualAction = action;
		result.contextualEnabledPredicate = enabled;
		result.contextualCheckedPredicate = checked;
		return result;
	}

	public function isEnabled(?context:CommandContext):Bool {
		if (contextualEnabledPredicate != null)
			return contextualEnabledPredicate(context == null ? new CommandContext() : context);
		return enabledPredicate == null || enabledPredicate();
	}

	public function isChecked(?context:CommandContext):Bool {
		if (contextualCheckedPredicate != null)
			return contextualCheckedPredicate(context == null ? new CommandContext() : context);
		return checkedPredicate != null && checkedPredicate();
	}

	/** Executes the action when its current predicate allows it. */
	public function execute():Bool {
		if (contextualAction != null)
			return executeContext(new CommandContext()).succeeded;
		if (!isEnabled())
			return false;
		action();
		return true;
	}

	/** Executes with typed document, selection, viewport, and parameter context. */
	public function executeContext(context:CommandContext):CommandResult {
		var actual = context == null ? new CommandContext() : context;
		if (!isEnabled(actual))
			return CommandResult.disabled();
		if (contextualAction == null) {
			action();
			return CommandResult.executed();
		}
		try {
			var result = contextualAction(actual);
			return result == null ? CommandResult.failed("Command returned no result") : result;
		} catch (error:Dynamic) {
			return CommandResult.failed(error);
		}
	}
}
