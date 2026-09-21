package nativekit.ui.core;

/**
 * Ordered application command registry. The last active scope wins, making
 * viewport, text-editor, and modal scopes able to override global shortcuts.
 */
class CommandRegistry {
	public static inline var GlobalScope:String = "global";
	final commands:Map<String, Command>;
	final commandScopes:Map<String, String>;
	final commandOrder:Array<String>;
	final activeScopeStack:Array<String>;
	public var revision(default, null):Int;

	public function new() {
		commands = new Map();
		commandScopes = new Map();
		commandOrder = [];
		activeScopeStack = [GlobalScope];
		revision = 0;
	}

	/** Registers a command without replacing an existing ID. */
	public function register(command:Command, scope:String = GlobalScope):Void {
		validateScope(scope);
		if (command == null)
			throw "Command registry cannot register null commands";
		if (commands.exists(command.id))
			throw 'Command ${command.id} is already registered';
		commands.set(command.id, command);
		commandScopes.set(command.id, scope);
		commandOrder.push(command.id);
		revision++;
	}

	public function unregister(id:String):Bool {
		if (id == null || !commands.exists(id))
			return false;
		commands.remove(id);
		commandScopes.remove(id);
		commandOrder.remove(id);
		revision++;
		return true;
	}

	public function get(id:String):Null<Command>
		return id == null ? null : commands.get(id);

	public function ids():Array<String>
		return commandOrder.copy();

	public function activeScopes():Array<String>
		return activeScopeStack.copy();

	/** Activates a context-specific scope until it is popped. */
	public function pushScope(scope:String):Void {
		validateScope(scope);
		if (scope == GlobalScope)
			return;
		activeScopeStack.push(scope);
		revision++;
	}

	public function popScope(scope:String):Bool {
		validateScope(scope);
		if (scope == GlobalScope)
			return false;
		var index = activeScopeStack.length - 1;
		while (index > 0) {
			if (activeScopeStack[index] == scope) {
				activeScopeStack.splice(index, 1);
				revision++;
				return true;
			}
			index--;
		}
		return false;
	}

	public function setActiveScopes(scopes:Array<String>):Void {
		activeScopeStack.resize(1);
		if (scopes != null)
			for (scope in scopes) {
				validateScope(scope);
				if (scope != GlobalScope)
					activeScopeStack.push(scope);
			}
		revision++;
	}

	/** Runs a callback with a temporary highest-priority scope. */
	public function withScope<T>(scope:String, callback:Void->T):T {
		if (callback == null)
			throw "Command scope callbacks cannot be null";
		pushScope(scope);
		try {
			var result = callback();
			popScope(scope);
			return result;
		} catch (error:Dynamic) {
			popScope(scope);
			throw error;
		}
	}

	/** Executes a command by ID when its current enabled predicate allows it. */
	public function execute(id:String):Bool {
		var command = get(id);
		if (command == null || !command.execute())
			return false;
		revision++;
		return true;
	}

	/** Executes a command by ID with an invocation context and structured result. */
	public function executeContext(id:String, context:CommandContext):CommandResult {
		var command = get(id);
		if (command == null)
			return CommandResult.notHandled();
		var result = command.executeContext(context);
		if (result.succeeded)
			revision++;
		return result;
	}

	/** Executes the highest-priority enabled command matching a key chord. */
	public function dispatch(key:Int, modifiers:Int):Bool {
		return dispatchContext(key, modifiers, null).succeeded;
	}

	/** Dispatches a key chord with context and returns the command outcome. */
	public function dispatchContext(key:Int, modifiers:Int,
			context:Null<CommandContext>):CommandResult {
		return dispatchContextInScopes(key, modifiers, context, null);
	}

	/**
	 * Dispatches through focused-node scopes before the registry's active scopes.
	 * The supplied path is ordered from root to focused node; the deepest scope
	 * therefore wins without mutating the registry's persistent scope stack.
	 */
	public function dispatchContextInScopes(key:Int, modifiers:Int,
			context:Null<CommandContext>, pathScopes:Null<Array<String>>):CommandResult {
		var normalized = Shortcut.normalizeModifiers(modifiers);
		var actual = context == null ? new CommandContext() : context;
		var scopes:Array<String> = [];
		var seen:Map<String, Bool> = new Map();
		if (pathScopes != null) {
			var pathIndex = pathScopes.length - 1;
			while (pathIndex >= 0) {
				var pathScope = pathScopes[pathIndex];
				if (pathScope != null && pathScope.length > 0) {
					validateScope(pathScope);
					if (!seen.exists(pathScope)) {
						scopes.push(pathScope);
						seen.set(pathScope, true);
					}
				}
				pathIndex--;
			}
		}
		var activeIndex = activeScopeStack.length - 1;
		while (activeIndex >= 0) {
			var activeScope = activeScopeStack[activeIndex];
			if (!seen.exists(activeScope)) {
				scopes.push(activeScope);
				seen.set(activeScope, true);
			}
			activeIndex--;
		}
		var disabled:Null<CommandResult> = null;
		for (scope in scopes) {
			var commandIndex = commandOrder.length - 1;
			while (commandIndex >= 0) {
				var id = commandOrder[commandIndex];
				var command = commands.get(id);
				if (command != null && commandScopes.get(id) == scope &&
					command.shortcut != null && command.shortcut.matches(key, normalized)) {
					if (!command.isEnabled(actual)) {
						disabled = CommandResult.disabled();
						break;
					}
					var result = command.executeContext(actual);
					if (result.succeeded) {
						revision++;
						return result;
					}
					return result;
				}
				commandIndex--;
			}
		}
		return disabled == null ? CommandResult.notHandled() : disabled;
	}

	/** Invalidates command-bound views after external predicate state changes. */
	public function refresh():Void
		revision++;

	/** Adds standard undo/redo commands to this registry. */
	public function installHistoryCommands(history:EditHistory,
			undoId:String = "edit.undo", redoId:String = "edit.redo"):Void {
		if (history == null)
			throw "History commands require an edit history";
		register(new Command(undoId, "Undo", function() { history.undo(); },
			new Shortcut(UiKey.Z, UiModifier.Control), function() return history.canUndo));
		register(new Command(redoId, "Redo", function() { history.redo(); },
			new Shortcut(UiKey.Y, UiModifier.Control), function() return history.canRedo));
	}

	/** Adds undo/redo commands that follow the document in the invocation context. */
	public function installDocumentHistoryCommands(document:EditorDocument,
			undoId:String = "edit.undo", redoId:String = "edit.redo"):Void {
		if (document == null)
			throw "Document history commands require a document";
		register(Command.contextual(undoId, "Undo", function(context) {
			if (context.document != document)
				return CommandResult.rejected("The active document changed");
			return document.undo() ? CommandResult.executed() : CommandResult.rejected("Nothing to undo");
		}, new Shortcut(UiKey.Z, UiModifier.Control), function(context) {
			return context.document == document && document.canUndo;
		}));
		register(Command.contextual(redoId, "Redo", function(context) {
			if (context.document != document)
				return CommandResult.rejected("The active document changed");
			return document.redo() ? CommandResult.executed() : CommandResult.rejected("Nothing to redo");
		}, new Shortcut(UiKey.Y, UiModifier.Control), function(context) {
			return context.document == document && document.canRedo;
		}));
	}

	function validateScope(scope:String):Void {
		if (scope == null || scope.length == 0)
			throw "Command scopes require a stable name";
	}
}
