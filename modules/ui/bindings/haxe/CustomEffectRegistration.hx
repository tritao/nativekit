import NativeKitUI;
import nativekit.ui.style.CustomEffectDefinition;

/**
 * Native shader implementation for a typed custom effect definition.
 *
 * The sources are final backend shader fragments supplied by the application;
 * the Haxe style object only carries the definition and values.
 */
class CustomEffectRegistration {
	public final definition:CustomEffectDefinition;
	public final glsl410Fragment:Null<String>;
	public final glsl300esFragment:Null<String>;
	public final hlsl5Fragment:Null<String>;
	public final metalMacosFragment:Null<String>;

	public function new(definition:CustomEffectDefinition, glsl410Fragment:Null<String> = null,
		glsl300esFragment:Null<String> = null, hlsl5Fragment:Null<String> = null,
		metalMacosFragment:Null<String> = null) {
		if (definition == null)
			throw "Custom effect registrations require a definition";
		if (glsl410Fragment == null && glsl300esFragment == null && hlsl5Fragment == null &&
			metalMacosFragment == null)
			throw "Custom effect registrations require at least one shader fragment";
		this.definition = definition;
		this.glsl410Fragment = glsl410Fragment;
		this.glsl300esFragment = glsl300esFragment;
		this.hlsl5Fragment = hlsl5Fragment;
		this.metalMacosFragment = metalMacosFragment;
	}

	@:allow(Renderer)
	private function nativeValue():nkui_custom_effect_registration {
		var result = new nkui_custom_effect_registration();
		result.set_registration_id(definition.id);
		result.set_name(definition.name);
		result.set_glsl410_fragment(glsl410Fragment);
		result.set_glsl300es_fragment(glsl300esFragment);
		result.set_hlsl5_fragment(hlsl5Fragment);
		result.set_metal_macos_fragment(metalMacosFragment);
		result.set_parameter_components(definition.componentCount);
		result.set_pass_count(definition.passCount);
		result.set_sampling_inputs(definition.samplingInputs);
		result.set_ink_overflow(0, definition.overflow.left);
		result.set_ink_overflow(1, definition.overflow.top);
		result.set_ink_overflow(2, definition.overflow.right);
		result.set_ink_overflow(3, definition.overflow.bottom);
		return result;
	}
}
