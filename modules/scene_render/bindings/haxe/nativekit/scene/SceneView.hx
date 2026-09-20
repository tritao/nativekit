package nativekit.scene;

import NativeKitScene;
import NativeKitSceneRender;

/** Immutable-at-render-boundary view configuration with typed overrides. */
class SceneView {
	final value:nkscene_render_view;
	var visibilityOverrides:Array<nkscene_render_visibility_override> = [];
	var materialOverrides:Array<nkscene_render_material_override> = [];
	var selectionOverrides:Array<nkscene_render_material_override> = [];
	var hoverOverrides:Array<nkscene_render_material_override> = [];
	var clipPlanes:Array<nkscene_render_clip_plane> = [];

	public function new() {
		value = new nkscene_render_view();
		value.set_struct_size(nkscene_render_view.size());
	}

	public function setRoot(root:Occurrence):SceneView {
		value.set_root(root.nativeValue());
		return this;
	}

	public function clearRoot():SceneView {
		value.set_root(new nkscene_occurrence_id());
		return this;
	}

	public function setIncludeInvisible(include:Bool):SceneView {
		value.set_include_invisible(include ? 1 : 0);
		return this;
	}

	/** Enables bounds culling and applies a column-major world-to-clip matrix. */
	public function setViewProjection(transform:Transform):SceneView {
		var camera = new nkscene_render_camera();
		camera.set_enabled(1);
		camera.set_view_projection(transform.nativeValue());
		value.set_camera(camera);
		return this;
	}

	/** Disables bounds culling and restores the default identity projection. */
	public function clearViewProjection():SceneView {
		value.set_camera(new nkscene_render_camera());
		return this;
	}

	/** Adds a conservative occurrence-level section plane. Kept side is normal * p + distance >= 0. */
	public function addClipPlane(normalX:Float, normalY:Float, normalZ:Float,
			distance:Float, enabled:Bool = true):SceneView {
		var plane = new nkscene_render_clip_plane();
		plane.set_normal(0, normalX);
		plane.set_normal(1, normalY);
		plane.set_normal(2, normalZ);
		plane.set_distance(distance);
		plane.set_enabled(enabled ? 1 : 0);
		clipPlanes.push(plane);
		value.set_clip_planes(clipPlanes);
		return this;
	}

	public function clearClipPlanes():SceneView {
		clipPlanes.resize(0);
		value.set_clip_planes(clipPlanes);
		return this;
	}

	public function setVisibility(occurrence:Occurrence, visible:Bool):SceneView {
		var stable = occurrence.stableValue();
		for (override in visibilityOverrides) {
			if (override.get_occurrence().get_value() == stable) {
				override.set_visible(visible ? 1 : 0);
				value.set_visibility_overrides(visibilityOverrides);
				return this;
			}
		}
		var override = new nkscene_render_visibility_override();
		override.set_occurrence(occurrence.nativeValue());
		override.set_visible(visible ? 1 : 0);
		visibilityOverrides.push(override);
		value.set_visibility_overrides(visibilityOverrides);
		return this;
	}

	public function setMaterial(occurrence:Occurrence, material:Material):SceneView {
		setMaterialOverride(materialOverrides, occurrence, material);
		value.set_material_overrides(materialOverrides);
		return this;
	}

	@:allow(SelectionSet)
	function setSelectionMaterial(occurrence:Occurrence, material:Material):SceneView {
		setMaterialOverride(selectionOverrides, occurrence, material);
		value.set_selection_overrides(selectionOverrides);
		return this;
	}

	@:allow(SceneInteraction)
	function setHoverMaterial(occurrence:Occurrence, material:Material):SceneView {
		setMaterialOverride(hoverOverrides, occurrence, material);
		value.set_hover_overrides(hoverOverrides);
		return this;
	}

	public function clearSelectionOverrides():SceneView {
		selectionOverrides.resize(0);
		value.set_selection_overrides(selectionOverrides);
		return this;
	}

	public function clearHoverOverrides():SceneView {
		hoverOverrides.resize(0);
		value.set_hover_overrides(hoverOverrides);
		return this;
	}

	public function clearOverrides():SceneView {
		visibilityOverrides.resize(0);
		materialOverrides.resize(0);
		selectionOverrides.resize(0);
		hoverOverrides.resize(0);
		value.set_visibility_overrides(visibilityOverrides);
		value.set_material_overrides(materialOverrides);
		value.set_selection_overrides(selectionOverrides);
		value.set_hover_overrides(hoverOverrides);
		return this;
	}

	public function applyVisibilityFilter(filter:VisibilityFilter):SceneView {
		filter.apply(this);
		return this;
	}

	public function applySelection(selection:SelectionSet, highlight:Material):SceneView {
		clearSelectionOverrides();
		selection.apply(this, highlight);
		return this;
	}

	/** Replaces the hover layer; hover takes precedence over selection. */
	public function applyHover(occurrence:Null<Occurrence>, highlight:Material):SceneView {
		clearHoverOverrides();
		if (occurrence != null)
			setHoverMaterial(occurrence, highlight);
		return this;
	}

	public function visibilityOverrideValues():Array<nkscene_render_visibility_override>
		return visibilityOverrides.copy();

	public function materialOverrideValues():Array<nkscene_render_material_override>
		return materialOverrides.copy();

	public function selectionOverrideValues():Array<nkscene_render_material_override>
		return selectionOverrides.copy();

	public function hoverOverrideValues():Array<nkscene_render_material_override>
		return hoverOverrides.copy();

	public function visibilityOverrideCount():Int
		return visibilityOverrides.length;

	public function materialOverrideCount():Int
		return materialOverrides.length + selectionOverrides.length + hoverOverrides.length;

	public function selectionOverrideCount():Int
		return selectionOverrides.length;

	public function hoverOverrideCount():Int
		return hoverOverrides.length;

	public function clipPlaneCount():Int
		return clipPlanes.length;

	@:allow(SceneRenderer)
	function nativeValue():nkscene_render_view
		return value;

	function setMaterialOverride(overrides:Array<nkscene_render_material_override>,
			occurrence:Occurrence, material:Material):Void {
		var stable = occurrence.stableValue(), materialValue = material.id();
		for (override in overrides) {
			if (override.get_occurrence().get_value() == stable) {
				override.set_material(materialValue);
				return;
			}
		}
		var override = new nkscene_render_material_override();
		override.set_occurrence(occurrence.nativeValue());
		override.set_material(materialValue);
		overrides.push(override);
	}
}
