package nativekit.scene;

import NativeKitGpu;
import NativeKitSceneRender;

/** Owns one asynchronous GPU ID-pass readback. */
class PickRequest {
	final renderer:SceneRenderer;
	final owner:Ownednkscene_render_pick_request;
	var disposed:Bool = false;

	@:allow(SceneRenderer)
	private function new(renderer:SceneRenderer, owner:Ownednkscene_render_pick_request) {
		this.renderer = renderer;
		this.owner = owner;
	}

	/** Polls without blocking. The request is valid until disposed. */
	public function poll(snapshot:Snapshot):PickPollResult {
		ensureLive();
		return renderer.pollPick(this, snapshot);
	}

	public function dispose():Void {
		if (disposed)
			return;
		owner.close();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SceneRenderer)
	function nativeHandle():nkscene_render_pick_request
		return owner.borrow();

	function ensureLive():Void {
		if (disposed)
			throw "Pick request has been disposed";
	}
}
