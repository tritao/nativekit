package nativekit.scene;

import NativeKitSceneRender;

/**
 * Coordinates the frame boundary between scene interaction and rendering.
 *
 * A presentation owns one interaction state object and one reusable view. Each
 * frame synchronizes that state with the immutable snapshot, advances the
 * asynchronous hover request, applies selection and hover layers, and then
 * renders the view.
 */
class ScenePresentation {
	public final interaction:SceneInteraction;
	public final view:SceneView;
	final selectionMaterial:Material;
	final hoverMaterial:Material;
	var disposed:Bool = false;

	private function new(interaction:SceneInteraction, view:SceneView,
			selectionMaterial:Material, hoverMaterial:Material) {
		this.interaction = interaction;
		this.view = view;
		this.selectionMaterial = selectionMaterial;
		this.hoverMaterial = hoverMaterial;
	}

	/** Creates a presentation with its own asynchronous interaction state. */
	public static function create(view:SceneView, selectionMaterial:Material,
			hoverMaterial:Material):ScenePresentation
		return new ScenePresentation(SceneInteraction.create(), view,
			selectionMaterial, hoverMaterial);

	/** Replaces the pending asynchronous GPU hover request. */
	public function requestHover(renderer:SceneRenderer, snapshot:Snapshot,
			width:Int, height:Int, x:Int, y:Int):Void {
		ensureLive();
		interaction.requestHover(renderer, snapshot, width, height, x, y);
	}

	/** Polls hover without applying presentation layers or rendering. */
	public function pollHover(renderer:SceneRenderer,
			snapshot:Snapshot):InteractionHoverResult {
		ensureLive();
		return interaction.pollHover(renderer, snapshot);
	}

	public function select(occurrence:Occurrence, mode:SelectionMode):Void {
		ensureLive();
		interaction.select(occurrence, mode);
	}

	public function clearSelection():Void {
		ensureLive();
		interaction.clearSelection();
	}

	/**
	 * Advances interaction state and renders one frame from an immutable
	 * snapshot. The view and render plan are reused across frames.
	 */
	public function render(renderer:SceneRenderer, snapshot:Snapshot,
			?changes:Null<ChangeSet>):nkscene_render_execution_stats {
		ensureLive();
		interaction.synchronize(snapshot);
		interaction.pollHover(renderer, snapshot);
		interaction.applySelection(view, selectionMaterial);
		interaction.applyHover(view, hoverMaterial);
		return renderer.render(snapshot, view, changes);
	}

	public function dispose():Void {
		if (disposed)
			return;
		interaction.dispose();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Scene presentation has been disposed";
	}
}
