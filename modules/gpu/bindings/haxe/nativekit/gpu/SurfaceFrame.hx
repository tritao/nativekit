package nativekit.gpu;
import nativekit.ffi.NativeKit;

import NativeKitError;

/**
 * Immutable surface target acquired for a deferred render submission.
 *
 * Acquire on the platform executor, submit the batch on the render executor,
 * then present or cancel this frame on the platform executor.
 */
class SurfaceFrame {
	final surface:Surface;
	final value:Dynamic;
	final target:Dynamic;
	var finished:Bool = false;

	function new(surface:Surface, value:Dynamic, target:Dynamic) {
		this.surface = surface;
		this.value = value;
		this.target = target;
	}

	/** Presents the submitted frame and releases its platform transaction. */
	public function present():Void {
		ensureOpen();
		var status = NativeKit.nk_surface_present_frame(value);
		if (status != Result.Ok)
			throw new NativeKitError(status, "surfaceFrame.present", NativeKit.nk_last_error());
		finished = true;
	}

	/** Cancels the frame and releases its platform transaction. */
	public function cancel():Void {
		ensureOpen();
		var status = NativeKit.nk_surface_cancel_frame(value);
		if (status != Result.Ok)
			throw new NativeKitError(status, "surfaceFrame.cancel", NativeKit.nk_last_error());
		finished = true;
	}

	public function isFinished():Bool
		return finished;

	@:allow(Batch)
	function nativeTarget():Dynamic {
		ensureOpen();
		return target;
	}

	@:allow(Batch)
	function ensureRenderer(renderer:Renderer):Void {
		ensureOpen();
		if (!renderer.ownsSurface(surface))
			throw "GPU surface frame belongs to another renderer";
	}

	function ensureOpen():Void {
		if (finished)
			throw "GPU surface frame has already finished";
		surface.ensureLive();
	}
}
