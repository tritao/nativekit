package nativekit.scene;

import NativeKitScene;

/** Fluent value builder for one material resource. */
class MaterialData {
	final value:nkscene_material_data;
	var flags:Int = 0;

	public function new() {
		value = new nkscene_material_data();
		value.set_struct_size(nkscene_material_data.size());
		setBaseColor(1.0, 1.0, 1.0, 1.0);
		setOpacity(1.0);
		setOpaque(true);
	}

	public static function opaque(red:Float, green:Float, blue:Float,
			alpha:Float = 1.0):MaterialData {
		return new MaterialData().setBaseColor(red, green, blue, alpha);
	}

	public function setBaseColor(red:Float, green:Float, blue:Float,
			alpha:Float = 1.0):MaterialData {
		value.set_base_color(0, red);
		value.set_base_color(1, green);
		value.set_base_color(2, blue);
		value.set_base_color(3, alpha);
		return this;
	}

	public function setOpacity(opacity:Float):MaterialData {
		value.set_opacity(opacity);
		return this;
	}

	public function setMetallic(value:Float):MaterialData {
		this.value.set_metallic(value);
		return this;
	}

	public function setRoughness(value:Float):MaterialData {
		this.value.set_roughness(value);
		return this;
	}

	public function setEmissive(red:Float, green:Float, blue:Float):MaterialData {
		value.set_emissive(0, red);
		value.set_emissive(1, green);
		value.set_emissive(2, blue);
		return this;
	}

	public function setAlphaCutoff(value:Float):MaterialData {
		this.value.set_alpha_cutoff(value);
		return this;
	}

	public function setAlphaMode(value:Int):MaterialData {
		this.value.set_alpha_mode(value);
		return this;
	}

	public function setBaseColorTexture(texture:Null<Texture>, sampler:Null<Sampler> = null):MaterialData {
		value.set_base_color_texture(texture == null ? new nkscene_texture_id() : texture.id());
		value.set_sampler(sampler == null ? new nkscene_sampler_id() : sampler.id());
		return this;
	}

	public function setMetallicRoughnessTexture(texture:Null<Texture>):MaterialData {
		value.set_metallic_roughness_texture(texture == null ? new nkscene_texture_id() : texture.id());
		return this;
	}

	public function setNormalTexture(texture:Null<Texture>):MaterialData {
		value.set_normal_texture(texture == null ? new nkscene_texture_id() : texture.id());
		return this;
	}

	public function setEmissiveTexture(texture:Null<Texture>):MaterialData {
		value.set_emissive_texture(texture == null ? new nkscene_texture_id() : texture.id());
		return this;
	}

	public function setOcclusionTexture(texture:Null<Texture>):MaterialData {
		value.set_occlusion_texture(texture == null ? new nkscene_texture_id() : texture.id());
		return this;
	}

	public function setOpaque(opaque:Bool):MaterialData {
		flags = opaque ? flags | NativeKitSceneConstants.NKS_MATERIAL_OPAQUE
			: flags & ~NativeKitSceneConstants.NKS_MATERIAL_OPAQUE;
		value.set_flags(flags);
		return this;
	}

	public function setDoubleSided(doubleSided:Bool):MaterialData {
		flags = doubleSided ? flags | NativeKitSceneConstants.NKS_MATERIAL_DOUBLE_SIDED
			: flags & ~NativeKitSceneConstants.NKS_MATERIAL_DOUBLE_SIDED;
		value.set_flags(flags);
		return this;
	}

	@:allow(Scene)
	function nativeValue():nkscene_material_data
		return value;
}
