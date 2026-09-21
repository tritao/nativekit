package nativekit.ui.core;

/**
 * Mutable editor workspace state independent from rendering. All mutations
 * increment revision and notify listeners so hosts can invalidate a frame.
 */
class DockWorkspaceModel {
	public final panels:Map<String, DockPanelDescriptor>;
	public var root(default, null):DockNode;
	public var defaultRoot(default, null):DockNode;
	public var activePanelId(default, null):Null<String>;
	public var revision(default, null):Int;
	final listeners:Array<Void->Void>;

	public function new(?defaultRoot:DockNode) {
		panels = new Map();
		root = DockNodeTools.normalize(defaultRoot == null ? DockNode.Empty : defaultRoot);
		this.defaultRoot = DockNodeTools.clone(root);
		activePanelId = DockNodeTools.firstPanel(root);
		revision = 0;
		listeners = [];
	}

	public function listen(callback:Void->Void):Void {
		if (callback != null && !listeners.contains(callback))
			listeners.push(callback);
	}

	public function register(panel:DockPanelDescriptor):Void {
		if (panel == null)
			throw "Dock workspace cannot register a null panel";
		if (panels.exists(panel.id))
			throw 'Dock panel ${panel.id} is already registered';
		panels.set(panel.id, panel);
		if (DockNodeTools.contains(root, panel.id) && activePanelId == null)
			activePanelId = panel.id;
	}

	public function get(panelId:String):Null<DockPanelDescriptor>
		return panelId == null ? null : panels.get(panelId);

	public function isOpen(panelId:String):Bool
		return DockNodeTools.contains(root, panelId);

	public function panelIds():Array<String>
		return DockNodeTools.panelIds(root);

	/** Installs a validated default layout and makes it the active layout. */
	public function setDefaultLayout(layout:DockNode):Void {
		validateLayout(layout);
		defaultRoot = DockNodeTools.clone(layout);
		setRoot(layout);
	}

	public function setRoot(layout:DockNode):Void {
		validateLayout(layout);
		root = DockNodeTools.normalize(DockNodeTools.clone(layout));
		activePanelId = chooseActive(activePanelId);
		touch();
	}

	public function reset():Void
		setRoot(defaultRoot);

	public function activate(panelId:String):Bool {
		if (!isOpen(panelId))
			return false;
		var changed = activePanelId != panelId;
		root = DockNodeTools.normalize(DockNodeTools.activate(root, panelId));
		activePanelId = panelId;
		if (changed)
			touch();
		return true;
	}

	public function close(panelId:String):Bool {
		var panel = get(panelId);
		if (panel == null || !panel.closable || !isOpen(panelId))
			return false;
		root = DockNodeTools.remove(root, panelId);
		if (activePanelId == panelId)
			activePanelId = DockNodeTools.firstPanel(root);
		touch();
		return true;
	}

	/** Reopens a closed panel into a target tab group or the first panel. */
	public function open(panelId:String, ?targetPanelId:String):Bool {
		if (get(panelId) == null)
			return false;
		if (isOpen(panelId))
			return activate(panelId);
		var target = targetPanelId != null && isOpen(targetPanelId) ? targetPanelId :
			DockNodeTools.firstPanel(root);
		if (target == null) {
			root = DockNode.Panel(panelId);
			activePanelId = panelId;
			touch();
			return true;
		}
		return dock(panelId, target, DockDropZone.Center);
	}

	public function dock(panelId:String, targetPanelId:String, zone:DockDropZone):Bool {
		if (get(panelId) == null || get(targetPanelId) == null || zone == null ||
			panelId == targetPanelId || !isOpen(targetPanelId))
			return false;
		var next = DockNodeTools.dock(root, panelId, targetPanelId, zone);
		if (DockNodeTools.validate(next, panelMap()) != null)
			return false;
		root = next;
		activePanelId = panelId;
		touch();
		return true;
	}

	public function setSplitRatio(path:Array<Int>, ratio:Float):Bool {
		var next = DockNodeTools.setSplitRatio(root, path, ratio);
		if (DockNodeTools.same(root, next))
			return false;
		root = next;
		touch();
		return true;
	}

	public function snapshot():DockWorkspaceSnapshot
		return new DockWorkspaceSnapshot(root, activePanelId);

	/** Serializes the current layout through the shared versioned codec. */
	public function snapshotJson():String
		return DockWorkspaceSnapshotCodec.encode(snapshot());

	/** Restores a serialized layout without coupling the UI module to storage. */
	public function restoreJson(source:String):Bool {
		var snapshot = DockWorkspaceSnapshotCodec.decode(source);
		return snapshot != null && restore(snapshot);
	}

	public function saveTo(storage:DockWorkspacePersistence, key:String):Void {
		if (storage == null || key == null || key.length == 0)
			throw "Dock workspace persistence requires storage and a stable key";
		storage.save(key, snapshotJson());
	}

	public function restoreFrom(storage:DockWorkspacePersistence, key:String):Bool {
		if (storage == null || key == null || key.length == 0)
			throw "Dock workspace persistence requires storage and a stable key";
		return restoreJson(storage.load(key));
	}

	public function restore(snapshot:DockWorkspaceSnapshot):Bool {
		if (snapshot == null || snapshot.version != DockWorkspaceSnapshot.CurrentVersion)
			return false;
		if (DockNodeTools.validate(snapshot.root, panelMap()) != null)
			return false;
		root = DockNodeTools.normalize(DockNodeTools.clone(snapshot.root));
		activePanelId = snapshot.activePanelId != null && isOpen(snapshot.activePanelId) ?
			snapshot.activePanelId : DockNodeTools.firstPanel(root);
		touch();
		return true;
	}

	/** Installs command-palette/menu actions for the active workspace. */
	public function installCommands(registry:CommandRegistry, prefix:String = "dock",
		scope:String = CommandRegistry.GlobalScope):Void {
		if (registry == null || prefix == null || prefix.length == 0)
			throw "Dock commands require a registry and stable prefix";
		registry.register(Command.contextual(prefix + ".close", "Close panel", function(context) {
			var panelId = context.parameters.getString("panel");
			if (panelId == null)
				panelId = activePanelId;
			return panelId != null && close(panelId) ? CommandResult.executed() :
				CommandResult.rejected("The active panel cannot be closed");
		}, null, function(context) {
			var panelId = context.parameters.getString("panel");
			if (panelId == null)
				panelId = activePanelId;
			var panel = panelId == null ? null : get(panelId);
			return panel != null && panel.closable && isOpen(panelId);
		}), scope);
		registry.register(Command.contextual(prefix + ".reset", "Reset workspace", function(_) {
			reset();
			return CommandResult.executed();
		}), scope);
	}

	public function unregister(panelId:String):Bool {
		if (!panels.exists(panelId))
			return false;
		panels.remove(panelId);
		root = DockNodeTools.remove(root, panelId);
		defaultRoot = DockNodeTools.remove(defaultRoot, panelId);
		if (activePanelId == panelId)
			activePanelId = DockNodeTools.firstPanel(root);
		touch();
		return true;
	}

	function validateLayout(layout:DockNode):Void {
		var error = DockNodeTools.validate(layout, panelMap());
		if (error != null)
			throw error;
	}

	function chooseActive(preferred:Null<String>):Null<String>
		return preferred != null && isOpen(preferred) ? preferred : DockNodeTools.firstPanel(root);

	function panelMap():Map<String, Bool> {
		var result:Map<String, Bool> = new Map();
		for (id in panels.keys())
			result.set(id, true);
		return result;
	}

	function touch():Void {
		revision++;
		var callbacks = listeners.copy();
		for (callback in callbacks)
			callback();
	}
}
