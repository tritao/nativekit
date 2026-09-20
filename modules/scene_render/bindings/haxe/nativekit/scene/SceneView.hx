package nativekit.scene;

import NativeKitScene;
import NativeKitSceneRender;

/** Immutable-at-render-boundary view configuration with typed overrides. */
class SceneView {
	final value:nkscene_render_view;
	var visibilityOverrides:Array<nkscene_render_visibility_override> = [];
	var materialOverrides:Array<nkscene_render_material_override> = [];

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

	public function setVisibility(occurrence:Occurrence, visible:Bool):SceneView {
		var override = new nkscene_render_visibility_override();
		override.set_occurrence(occurrence.nativeValue());
		override.set_visible(visible ? 1 : 0);
		visibilityOverrides.push(override);
		value.set_visibility_overrides(visibilityOverrides);
		return this;
	}

	public function setMaterial(occurrence:Occurrence, material:Material):SceneView {
		var override = new nkscene_render_material_override();
		override.set_occurrence(occurrence.nativeValue());
		override.set_material(material.id());
		materialOverrides.push(override);
		value.set_material_overrides(materialOverrides);
		return this;
	}

	public function clearOverrides():SceneView {
		visibilityOverrides.resize(0);
		materialOverrides.resize(0);
		value.set_visibility_overrides(visibilityOverrides);
		value.set_material_overrides(materialOverrides);
		return this;
	}

	public function applyVisibilityFilter(filter:VisibilityFilter):SceneView {
		filter.apply(this);
		return this;
	}

	public function applySelection(selection:SelectionSet, highlight:Material):SceneView {
		selection.apply(this, highlight);
		return this;
	}

	public function visibilityOverrideValues():Array<nkscene_render_visibility_override>
		return visibilityOverrides.copy();

	public function materialOverrideValues():Array<nkscene_render_material_override>
		return materialOverrides.copy();

	public function visibilityOverrideCount():Int
		return visibilityOverrides.length;

	public function materialOverrideCount():Int
		return materialOverrides.length;

	@:allow(SceneRenderer)
	function nativeValue():nkscene_render_view
		return value;
}
