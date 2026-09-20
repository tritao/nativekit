package nativekit.gpu;

import NativeKitGpu;
import nativekit.gpu.Enums.LoadAction;
import nativekit.gpu.Enums.StoreAction;

/** Clear and store behavior for one render-pass attachment. */
class AttachmentAction {
	public var loadAction:LoadAction;
	public var storeAction:StoreAction;
	public var clearRed:Float;
	public var clearGreen:Float;
	public var clearBlue:Float;
	public var clearAlpha:Float;
	public var clearDepth:Float;
	public var clearStencil:Int;

	public function new(loadAction:LoadAction = LoadAction.Load,
		storeAction:StoreAction = StoreAction.Store) {
		this.loadAction = loadAction;
		this.storeAction = storeAction;
		clearRed = 0.0;
		clearGreen = 0.0;
		clearBlue = 0.0;
		clearAlpha = 0.0;
		clearDepth = 1.0;
		clearStencil = 0;
	}

	@:allow(RenderPassDesc)
	function nativeValue():nkgpu_attachment_action {
		var value = new nkgpu_attachment_action();
		value.set_load_action(loadAction);
		value.set_store_action(storeAction);
		var color = new nkgpu_color();
		color.set_r(clearRed);
		color.set_g(clearGreen);
		color.set_b(clearBlue);
		color.set_a(clearAlpha);
		value.set_clear_color(color);
		value.set_clear_depth(clearDepth);
		value.set_clear_stencil(clearStencil);
		return value;
	}
}
