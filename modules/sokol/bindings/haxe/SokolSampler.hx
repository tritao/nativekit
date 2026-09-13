import SokolEnums.SokolFilter;
import SokolEnums.SokolWrap;

/** Renderer-owned texture sampler with typed filter and wrap modes. */
class SokolSampler {
	final renderer:SokolRenderer;
	final value:nks_sampler;
	var disposed:Bool = false;

	private function new(renderer:SokolRenderer, value:nks_sampler) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function create(renderer:SokolRenderer, minFilter:SokolFilter = SokolFilter.Nearest,
			magFilter:SokolFilter = SokolFilter.Nearest, wrapU:SokolWrap = SokolWrap.ClampToEdge,
			wrapV:SokolWrap = SokolWrap.ClampToEdge):SokolSampler {
		var made = NativeKitSokol.nks_sampler_create(renderer.nativeHandle(), minFilter, magFilter, wrapU, wrapV);
		SokolResult.check(made.status, "sampler.create");
		return new SokolSampler(renderer, made.out_sampler);
	}

	public function nativeHandle():nks_sampler {
		ensureLive();
		return value;
	}

	public function apply(slot:Int):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0)
			throw "Sokol sampler slot must be non-negative";
		SokolResult.check(NativeKitSokol.nks_apply_sampler(renderer.nativeHandle(), slot, value), "sampler.apply");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		SokolResult.check(NativeKitSokol.nks_sampler_destroy(renderer.nativeHandle(), value), "sampler.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SokolCommandBuffer)
	function rendererOwner():SokolRenderer
		return renderer;

	@:allow(SokolRenderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "Sokol sampler has been disposed";
		renderer.ensureResourceOperation();
	}
}
