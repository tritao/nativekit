package nativekit.gpu;

import nativekit.gpu.GpuResult;

import nativekit.ffi.NativeKitGpu;
import nativekit.gpu.Enums.BatchPassKind;

/** Reusable sealed submission batch owned by one renderer. */
class Batch {
	final renderer:Renderer;
	final value:nkgpu_batch;
	var sealed:Bool = false;
	var disposed:Bool = false;

	private function new(renderer:Renderer, value:nkgpu_batch) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function begin(renderer:Renderer):Batch {
		renderer.ensureLive();
		var made = NativeKitGpu.nkgpu_batch_begin(renderer.nativeHandle());
		GpuResult.check(made.status, "batch.begin");
		return new Batch(renderer, made.out_batch);
	}

	/** Appends a window-surface pass to this batch. */
	public function windowPass(width:Int, height:Int, clear:Bool = true):Batch {
		appendPass(BatchPassKind.Window, width, height, clear);
		return this;
	}

	/** Appends a compute pass to this batch. */
	public function computePass():Batch {
		appendPass(BatchPassKind.Compute, 0, 0, false);
		return this;
	}

	/** Appends a general attachment-based render pass to this batch. */
	public function renderPass(desc:RenderPassDesc):Batch {
		ensureMutable();
		if (desc == null)
			throw "GPU batch render-pass descriptor must not be null";
		GpuResult.check(NativeKitGpu.nkgpu_batch_append_render_pass(value, desc.nativeValue()),
			"batch.renderPass");
		return this;
	}

	/** Appends a copy/transfer pass to this batch. */
	public function copyPass():Batch {
		appendPass(BatchPassKind.Copy, 0, 0, false);
		return this;
	}

	/** Appends a packed command buffer to the most recent pass. */
	public function commands(commands:CommandBuffer):Batch {
		ensureMutable();
		if (commands == null || commands.size() <= 0)
			throw "GPU batch command buffer must not be empty";
		commands.ensureRenderer(renderer);
		GpuResult.check(NativeKitGpu.nkgpu_batch_append_command(value, commands.data(), commands.size()),
			"batch.commands");
		return this;
	}

	/** Freezes this batch; repeated sealing is safe. */
	public function seal():Batch {
		ensureLive();
		GpuResult.check(NativeKitGpu.nkgpu_batch_seal(value), "batch.seal");
		sealed = true;
		return this;
	}

	/** Replays a sealed batch against the renderer's current surface frame. */
	public function submit(?frame:SurfaceFrame):Void {
		ensureLive();
		if (!sealed)
			throw "GPU batch must be sealed before submission";
		renderer.ensureResourceOperation();
		if (frame == null)
			GpuResult.check(NativeKitGpu.nkgpu_batch_submit(renderer.nativeHandle(), value, null),
				"batch.submit");
		else {
			frame.ensureRenderer(renderer);
			GpuResult.check(NativeKitGpu.nkgpu_batch_submit(renderer.nativeHandle(), value,
				frame.nativeTarget()), "batch.submit");
		}
	}

	public function dispose():Void {
		if (disposed)
			return;
		GpuResult.check(NativeKitGpu.nkgpu_batch_destroy(value), "batch.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	function appendPass(kind:BatchPassKind, width:Int, height:Int, clear:Bool):Void {
		ensureMutable();
		if (kind == BatchPassKind.Window && (width <= 0 || height <= 0))
			throw "GPU batch window-pass dimensions must be positive";
		var pass = new nkgpu_batch_pass();
		pass.set_struct_size(32);
		pass.set_kind(kind);
		pass.set_clear(clear ? 1 : 0);
		pass.set_width(width);
		pass.set_height(height);
		GpuResult.check(NativeKitGpu.nkgpu_batch_append_pass(value, pass), "batch.appendPass");
	}

	function ensureMutable():Void {
		ensureLive();
		if (sealed)
			throw "GPU batch has been sealed";
	}

	function ensureLive():Void {
		if (disposed)
			throw "GPU batch has been disposed";
		renderer.ensureLive();
	}
}
