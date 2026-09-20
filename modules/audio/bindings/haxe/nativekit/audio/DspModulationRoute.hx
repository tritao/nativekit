package nativekit.audio;

import NativeKitAudio;

/** One typed source-to-destination modulation route in a DSP patch. */
class DspModulationRoute {
	public final source:DspModulationSource;
	public final destination:DspModulationDestination;
	public final amount:Float;
	public final polarity:DspModulationPolarity;

	public function new(source:DspModulationSource, destination:DspModulationDestination,
		amount:Float, ?polarity:DspModulationPolarity) {
		this.source = source;
		this.destination = destination;
		this.amount = amount;
		this.polarity = polarity == null ? DspModulationPolarity.Bipolar : polarity;
	}

	@:allow(nativekit.audio.DspPatchBuilder)
	private function nativeValue():NativeKitAudio.NativeDspModulationRouteOptions {
		var result = new NativeKitAudio.NativeDspModulationRouteOptions();
		result.set_struct_size(NativeKitAudio.NativeDspModulationRouteOptions.size());
		result.set_source(source);
		result.set_destination(destination);
		result.set_polarity(polarity);
		result.set_amount(amount);
		return result;
	}
}
