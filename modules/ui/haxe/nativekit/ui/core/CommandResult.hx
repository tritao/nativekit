package nativekit.ui.core;

/** Outcome returned by a contextual command invocation. */
enum CommandStatus {
	Executed;
	NotHandled;
	Disabled;
	Rejected(message:String);
	Cancelled(message:String);
	Failed(message:String);
}

/** Structured command outcome suitable for menus, palettes, and automation. */
class CommandResult {
	public final status:CommandStatus;
	public final changed:Bool;

	public function new(status:CommandStatus, changed:Bool = false) {
		this.status = status;
		this.changed = changed;
	}

	public var succeeded(get, never):Bool;
	inline function get_succeeded():Bool
		return switch (status) {
			case Executed: true;
			default: false;
		};

	public var message(get, never):Null<String>;
	function get_message():Null<String>
		return switch (status) {
			case Rejected(value) | Cancelled(value) | Failed(value): value;
			default: null;
		};

	public static function executed(changed:Bool = true):CommandResult
		return new CommandResult(Executed, changed);

	public static function notHandled():CommandResult
		return new CommandResult(NotHandled);

	public static function disabled():CommandResult
		return new CommandResult(Disabled);

	public static function rejected(message:String = "Command rejected"):CommandResult
		return new CommandResult(Rejected(message));

	public static function cancelled(message:String = "Command cancelled"):CommandResult
		return new CommandResult(Cancelled(message));

	public static function failed(error:Dynamic):CommandResult
		return new CommandResult(Failed(error == null ? "Command failed" : Std.string(error)));
}
