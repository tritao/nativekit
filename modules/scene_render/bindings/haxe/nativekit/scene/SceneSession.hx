package nativekit.scene;

import nativekit.gpu.Renderer;

/**
 * Thin Haxeon owner for the NativeKit scene/render frame loop.
 *
 * The session owns the mutable scene, render boundary, and reusable view.
 * Individual SceneFrame values remain caller-owned so an application can
 * retain immutable frames for asynchronous work.
 */
class SceneSession {
	final sceneValue:Scene;
	final rendererValue:SceneRenderer;
	final viewValue:SceneView;
	var disposed:Bool = false;

	private function new(scene:Scene, renderer:SceneRenderer, view:SceneView) {
		sceneValue = scene;
		rendererValue = renderer;
		viewValue = view;
	}

	/** Creates a session using the headless resource and command executor. */
	public static function createHeadless():SceneSession
		return new SceneSession(Scene.create(), SceneRenderer.createHeadless(), new SceneView());

	/** Creates a session attached to an externally owned GPU renderer. */
	public static function create(renderer:Renderer):SceneSession
		return new SceneSession(Scene.create(), SceneRenderer.create(renderer), new SceneView());

	public function scene():Scene {
		ensureLive();
		return sceneValue;
	}

	public function renderer():SceneRenderer {
		ensureLive();
		return rendererValue;
	}

	/** Returns the reusable view applied to subsequent render calls. */
	public function view():SceneView {
		ensureLive();
		return viewValue;
	}

	public function beginTransaction():Transaction
		return scene().beginTransaction();

	/** Captures the current published scene without a change set. */
	public function snapshotFrame():SceneFrame
		return SceneFrame.snapshot(scene());

	/** Commits a transaction and captures its corresponding render frame. */
	public function commit(transaction:Transaction):SceneFrame
		return transaction.commitFrame();

	/** Executes the current render plan against one immutable frame. */
	public function render(frame:SceneFrame):nkscene_render_execution_stats
		return renderer().renderFrame(frame, view());

	/** Performs a synchronous pixel pick against one immutable frame. */
	public function pickPixel(frame:SceneFrame, width:Int, height:Int,
			x:Int, y:Int):PickResult
		return renderer().pickPixel(frame.sceneSnapshot(), width, height, x, y);

	/** Starts an asynchronous pixel pick against one immutable frame. */
	public function pickPixelAsync(frame:SceneFrame, width:Int, height:Int,
			x:Int, y:Int):PickRequest
		return renderer().pickPixelAsync(frame.sceneSnapshot(), width, height, x, y);

	public function dispose():Void {
		if (disposed)
			return;
		rendererValue.dispose();
		sceneValue.dispose();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Scene session has been disposed";
	}
}
