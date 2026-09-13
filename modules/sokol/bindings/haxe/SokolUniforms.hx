/** One consumed uniform block staged for the current renderer frame. */
class SokolUniforms {
	final renderer:SokolRenderer;
	final value:nks_uniform_builder;
	var consumed:Bool = false;

	@:allow(SokolRenderer)
	private function new(renderer:SokolRenderer, value:nks_uniform_builder) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public function writeFloat(offset:Int, number:Float):SokolUniforms {
		ensureLive();
		if (offset < 0)
			throw "Sokol uniform offset must be non-negative";
		SokolResult.check(NativeKitSokol.nks_uniforms_write_f32(value, offset, number), "uniforms.writeFloat");
		return this;
	}

	public function apply(slot:Int):Void {
		ensureLive();
		if (slot < 0)
			throw "Sokol uniform slot must be non-negative";
		SokolResult.check(NativeKitSokol.nks_apply_uniforms(renderer.nativeHandle(), slot, value), "uniforms.apply");
		consumed = true;
	}

	@:allow(SokolRenderer)
	function rendererClosed():Void
		consumed = true;

	function ensureLive():Void {
		if (consumed)
			throw "Sokol uniform builder has been consumed";
		renderer.ensureFrame();
	}
}
