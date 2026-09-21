package nativekit.ui.widgets;

import Insets;
import LayoutAxis;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Command;
import nativekit.ui.core.CommandContext;
import nativekit.ui.core.CommandRegistry;
import nativekit.ui.core.CommandResult;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.View;

/** Searchable, virtualized command launcher suitable for editor key palettes. */
class CommandPalette implements View {
	public final key:String;
	public final registry:Null<CommandRegistry>;
	public final invocationContext:Null<CommandContext>;
	public final x:Float;
	public final y:Float;
	public var query:String;
	public var onDismiss:Null<Void->Void>;
	public var onResult:Null<CommandResult->Void>;

	public function new(key:String, ?registry:CommandRegistry, ?invocationContext:CommandContext,
			x:Float = 0.0, y:Float = 0.0, query:String = "",
			?onDismiss:Void->Void, ?onResult:CommandResult->Void) {
		if (key == null || key.length == 0)
			throw "Command palettes require a stable key";
		this.key = key;
		this.registry = registry;
		this.invocationContext = invocationContext;
		this.x = x;
		this.y = y;
		this.query = query == null ? "" : query;
		this.onDismiss = onDismiss;
		this.onResult = onResult;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var commands = registry == null ? context.commands : registry;
			var actualContext = invocationContext == null ? context.commandContext : invocationContext;
			var queryState:State<String> = context.state(context.id("query"), query);
			query = queryState.value;
			var model = new CommandPaletteModel(commands, actualContext, query);
			var searchStyle = new LayoutStyle();
			searchStyle.width = LayoutAxis.grow();
			var search = new SearchField("search", query, function(next) {
				query = next;
				queryState.update(next);
			}, searchStyle, "Search commands");
			var listStyle = new LayoutStyle();
			listStyle.width = LayoutAxis.grow();
			listStyle.height = LayoutAxis.fixed(320.0);
			var list = new ListView("commands", model, listStyle, null, 320.0, 0,
				null, function(index) {
					if (index < 0 || index >= model.commands.length)
						return;
					var result = commands.executeContext(model.commands[index].id, actualContext);
					if (onResult != null)
						onResult(result);
					if (result.succeeded && onDismiss != null)
						onDismiss();
				});
			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.fixed(480.0);
			contentStyle.padding = new Insets(12.0, 12.0, 12.0, 12.0);
			contentStyle.childGap = 8.0;
			contentStyle.background = context.theme.panelBackground;
			var content = new Column("content", [
				new KeyedView("search", search),
				new KeyedView("commands", list)
			], contentStyle);
			var popup = new Popup(key, content, x, y, null, onDismiss);
			popup.label = "Command palette";
			popup.modal = true;
			popup.dimBackdrop = true;
			return popup.build(context);
		});
	}
}

private class CommandPaletteModel implements ListViewModel {
	public final commands:Array<Command>;
	final context:CommandContext;

	public function new(registry:CommandRegistry, context:CommandContext, query:String) {
		commands = [];
		this.context = context == null ? new CommandContext() : context;
		var needle = query == null ? "" : query.toLowerCase();
		for (id in registry.ids()) {
			var command = registry.get(id);
			if (command != null && (needle.length == 0 || matches(command, needle)))
				commands.push(command);
		}
	}

	public function count():Int
		return commands.length;

	public function keyAt(index:Int):String
		return commands[index].id;

	public function extentAt(index:Int):Float
		return 36.0;

	public function buildItem(index:Int):View {
		var command = commands[index];
		var button = new Button(command.label, null, null, command.id);
		button.enabled = command.isEnabled(context);
		button.selected = command.isChecked(context);
		button.classes = ["command-palette-item"];
		return button;
	}

	public function revision():Int
		return 0;

	static function matches(command:Command, needle:String):Bool {
		return command.id.toLowerCase().indexOf(needle) >= 0 ||
			command.label.toLowerCase().indexOf(needle) >= 0;
	}
}
