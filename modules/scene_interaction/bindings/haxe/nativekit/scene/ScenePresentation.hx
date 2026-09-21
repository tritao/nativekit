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
	var sourceFilter:Null<SourceEntityFilter> = null;
	final isolatedOccurrenceFallback:Array<Occurrence> = [];
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

	/** Installs a persistent source filter for this presentation. */
	public function setSourceFilter(filter:Null<SourceEntityFilter>):ScenePresentation {
		ensureLive();
		sourceFilter = filter;
		isolatedOccurrenceFallback.resize(0);
		view.clearIsolation();
		if (filter == null) {
			view.clearSourceFilter();
		} else {
			filter.applyTo(view);
		}
		return this;
	}

	public function clearSourceFilter():ScenePresentation
		return setSourceFilter(null);

	public function hideSource(source:haxe.Int64):ScenePresentation {
		ensureLive();
		ensureSourceFilter().hideSource(source);
		applySourceFilter();
		return this;
	}

	public function showSource(source:haxe.Int64):ScenePresentation {
		ensureLive();
		ensureSourceFilter().showSource(source);
		applySourceFilter();
		return this;
	}

	public function setSourceMaterial(source:haxe.Int64,
			material:Material):ScenePresentation {
		ensureLive();
		ensureSourceFilter().setSourceMaterial(source, material);
		applySourceFilter();
		return this;
	}

	public function isolateSource(source:haxe.Int64):ScenePresentation {
		ensureLive();
		isolatedOccurrenceFallback.resize(0);
		ensureSourceFilter().isolateSource(source);
		applySourceFilter();
		return this;
	}

	public function isolateSources(sources:Array<haxe.Int64>):ScenePresentation {
		ensureLive();
		isolatedOccurrenceFallback.resize(0);
		ensureSourceFilter().isolateSources(sources);
		applySourceFilter();
		return this;
	}

	public function clearIsolation():ScenePresentation {
		ensureLive();
		if (sourceFilter != null) {
			sourceFilter.clearIsolation();
		}
		isolatedOccurrenceFallback.resize(0);
		view.clearIsolation();
		applySourceFilter();
		return this;
	}

	/** Isolates the source entities represented by the current selection. */
	public function isolateSelection(snapshot:Snapshot):ScenePresentation {
		ensureLive();
		var sources:Array<haxe.Int64> = [];
		isolatedOccurrenceFallback.resize(0);
		for (occurrence in interaction.selected()) {
			var info = snapshot.find(occurrence);
			if (info == null)
				continue;
			var source = info.sourceValue();
			if (source == haxe.Int64.ofInt(0)) {
				isolatedOccurrenceFallback.push(occurrence);
				continue;
			}
			var found = false;
			for (existing in sources)
				if (existing == source) {
					found = true;
					break;
				}
			if (!found)
				sources.push(source);
		}
		ensureSourceFilter().isolateSources(sources);
		applySourceFilter();
		return this;
	}

	/**
	 * Advances interaction state and renders one frame from an immutable
	 * snapshot. The view and render plan are reused across frames.
	 */
	public function render(renderer:SceneRenderer, snapshot:Snapshot,
			?changes:Null<ChangeSet>):nkscene_render_execution_stats {
		ensureLive();
		interaction.synchronize(snapshot);
		applySourceFilter();
		if (renderer.hasPlan())
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

	function ensureSourceFilter():SourceEntityFilter {
		var result = sourceFilter;
		if (result == null) {
			result = new SourceEntityFilter();
			sourceFilter = result;
		}
		return result;
	}

	function applySourceFilter():Void {
		if (sourceFilter != null)
			sourceFilter.applyTo(view);
		for (occurrence in isolatedOccurrenceFallback)
			view.setIsolatedOccurrence(occurrence, true);
	}
}
