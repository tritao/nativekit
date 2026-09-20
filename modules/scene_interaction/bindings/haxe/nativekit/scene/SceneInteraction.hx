package nativekit.scene;

import NativeKitGpu;
import NativeKitScene;
import NativeKitSceneInteraction;

/**
 * Owns asynchronous hover and selection state without mutating the Scene.
 * The renderer and snapshot remain owned by the caller.
 */
class SceneInteraction {
	final owner:Ownednkscene_interaction;
	var disposed:Bool = false;

	private function new(owner:Ownednkscene_interaction) {
		this.owner = owner;
	}

	public static function create():SceneInteraction {
		var made = NativeKitSceneInteraction.nkscene_interaction_create();
		check(made.status, "sceneInteraction.create");
		return new SceneInteraction(made.out_interaction);
	}

	/** Replaces any pending request with a new asynchronous GPU hover query. */
	public function requestHover(renderer:SceneRenderer, snapshot:Snapshot,
			width:Int, height:Int, x:Int, y:Int):Void {
		ensureLive();
		var result = NativeKitSceneInteraction.nkscene_interaction_request_hover(
			owner.borrow(), renderer.nativeExecutor(), renderer.nativePlan(), snapshot.nativeHandle(),
			width, height, x, y);
		check(result, "sceneInteraction.requestHover");
	}

	/** Polls the current hover request without blocking. */
	public function pollHover(renderer:SceneRenderer, snapshot:Snapshot):InteractionHoverResult {
		ensureLive();
		var result = NativeKitSceneInteraction.nkscene_interaction_poll_hover(
			owner.borrow(), renderer.nativeExecutor(), renderer.nativePlan(), snapshot.nativeHandle());
		check(result.status, "sceneInteraction.pollHover");
		return switch (result.out_state) {
			case 0: Idle;
			case 1: Pending;
			case 2:
				Ready(new PickResult(result.out_result));
			case 3: Stale;
			default: Failed(result.out_error);
		};
	}

	public function cancelHover():Void {
		ensureLive();
		NativeKitSceneInteraction.nkscene_interaction_cancel_hover(owner.borrow());
	}

	public function applyPick(pick:PickResult, mode:SelectionMode):Void {
		ensureLive();
		check(NativeKitSceneInteraction.nkscene_interaction_apply_pick(
			owner.borrow(), pick.nativeValue(), mode), "sceneInteraction.applyPick");
	}

	public function select(occurrence:Occurrence, mode:SelectionMode):Void {
		ensureLive();
		check(NativeKitSceneInteraction.nkscene_interaction_select(
			owner.borrow(), occurrence.nativeValue(), mode), "sceneInteraction.select");
	}

	public function clearSelection():Void {
		ensureLive();
		NativeKitSceneInteraction.nkscene_interaction_clear_selection(owner.borrow());
	}

	/** Removes selected or hovered occurrences that are absent from this snapshot. */
	public function synchronize(snapshot:Snapshot):Void {
		ensureLive();
		check(NativeKitSceneInteraction.nkscene_interaction_synchronize(
			owner.borrow(), snapshot.nativeHandle()), "sceneInteraction.synchronize");
	}

	public function hovered():Null<PickResult> {
		ensureLive();
		var result = NativeKitSceneInteraction.nkscene_interaction_get_hover(owner.borrow());
		check(result.status, "sceneInteraction.hovered");
		return result.out_has_hover == 0 ? null : new PickResult(result.out_result);
	}

	public function selected():Array<Occurrence> {
		ensureLive();
		var count = NativeKitSceneInteraction.nkscene_interaction_get_selection_count(owner.borrow());
		check(count.status, "sceneInteraction.selected");
		var result:Array<Occurrence> = [];
		for (index in 0...haxe.Int64.toInt(count.out_count)) {
			var value = NativeKitSceneInteraction.nkscene_interaction_get_selected(owner.borrow(), index);
			check(value.status, "sceneInteraction.selectedOccurrence");
			result.push(Occurrence.fromNative(value.out_occurrence));
		}
		return result;
	}

	/** Converts the current native selection into the existing view presentation set. */
	public function selectionSet():SelectionSet {
		var result = new SelectionSet();
		for (occurrence in selected())
			result.add(occurrence);
		return result;
	}

	/** Applies the current selection as material overrides on a view builder. */
	public function applySelection(view:SceneView, highlight:Material):SceneView
		return view.applySelection(selectionSet(), highlight);

	public function isSelected(occurrence:Occurrence):Bool {
		ensureLive();
		var result = NativeKitSceneInteraction.nkscene_interaction_is_selected(
			owner.borrow(), occurrence.nativeValue());
		check(result.status, "sceneInteraction.isSelected");
		return result.out_selected != 0;
	}

	public function dispose():Void {
		if (disposed)
			return;
		owner.close();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Scene interaction has been disposed";
	}

	static function check(status:Int, operation:String):Void {
		if (status != NativeKitSceneConstants.NKS_OK)
			throw '$operation failed with NativeKit scene status $status';
	}
}
