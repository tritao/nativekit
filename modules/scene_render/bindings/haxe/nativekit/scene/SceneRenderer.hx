package nativekit.scene;

import NativeKitGpu;
import NativeKitScene;
import NativeKitSceneRender;
import nativekit.gpu.GpuResult;
import nativekit.gpu.Renderer;

/**
 * Keeps render-plan and NativeKit GPU synchronization behind one explicit
 * render boundary. Scene snapshots and change sets are owned by the caller.
 */
class SceneRenderer {
	final gpuOwner:Null<Renderer>;
	var executor:Ownednkscene_render_executor;
	var planOwner:Null<Ownednkscene_render_plan> = null;
	var lastUpdateValue:Null<nkscene_render_update> = null;
	var disposed:Bool = false;

	private function new(gpuOwner:Null<Renderer>, renderer:nkgpu_renderer) {
		this.gpuOwner = gpuOwner;
		var made = NativeKitSceneRender.nkscene_render_executor_create(renderer);
		checkScene(made.status, "sceneRenderer.create");
		executor = made.out_executor;
	}

	/** Creates a renderer attached to a live NativeKit GPU renderer. */
	public static function create(renderer:Renderer):SceneRenderer
		return new SceneRenderer(renderer, renderer.nativeHandle());

	/** Creates the headless resource/command executor used by tests and tools. */
	public static function createHeadless():SceneRenderer
		return new SceneRenderer(null, new nkgpu_renderer());

	/** Compiles, refreshes, or incrementally updates the plan, then executes it. */
	public function render(snapshot:Snapshot, view:SceneView,
			?changes:Null<ChangeSet>):nkscene_render_execution_stats {
		ensureLive();
		var snapshotValue = snapshot.nativeHandle(),
			viewValue = view.nativeValue();
		if (planOwner == null) {
			var compiled = NativeKitSceneRender.nkscene_render_plan_compile(snapshotValue, viewValue);
			checkScene(compiled.status, "sceneRenderer.compile");
			planOwner = compiled.out_plan;
			lastUpdateValue = null;
		} else if (changes != null) {
			var updated = new nkscene_render_update();
			updated.set_struct_size(nkscene_render_update.size());
			var changeSet = changes.nativeHandle();
			var updateResult = NativeKitSceneRender.nkscene_render_plan_update(
				planOwner.borrow(), snapshotValue, changeSet, viewValue, updated);
			checkScene(updateResult.status, "sceneRenderer.update");
			lastUpdateValue = updateResult.out_update;
		} else {
			var refreshed = new nkscene_render_update();
			refreshed.set_struct_size(nkscene_render_update.size());
			var refreshResult = NativeKitSceneRender.nkscene_render_plan_refresh(
				planOwner.borrow(), snapshotValue, viewValue, refreshed);
			checkScene(refreshResult.status, "sceneRenderer.refresh");
			lastUpdateValue = refreshResult.out_update;
		}

		var executed = NativeKitSceneRender.nkscene_render_executor_execute(
			executor.borrow(), planOwner.borrow(), snapshotValue);
		checkScene(executed.status, "sceneRenderer.execute");
		GpuResult.check(executed.out_stats.get_result(), "sceneRenderer.execute");
		return executed.out_stats;
	}

	/** Returns the last plan update metrics, or null before the first update. */
	public function lastUpdate():Null<nkscene_render_update>
		return lastUpdateValue;

	/** Performs a GPU ID pass and resolves one pixel to scene ownership. */
	public function pickPixel(snapshot:Snapshot, width:Int, height:Int, x:Int, y:Int):nkscene_render_pick_result {
		ensureLive();
		if (planOwner == null)
			throw "sceneRenderer.pickPixel requires a compiled render plan";
		var picked = NativeKitSceneRender.nkscene_render_executor_pick_pixel(
			executor.borrow(), planOwner.borrow(), snapshot.nativeHandle(), width, height, x, y);
		if (picked.status != 0) {
			var last = NativeKitSceneRender.nkscene_render_executor_get_last_result(executor.borrow());
			checkScene(last.status, "sceneRenderer.pickPixel");
			GpuResult.check(last.out_result, "sceneRenderer.pickPixel");
			throw "sceneRenderer.pickPixel failed";
		}
		return picked.out_result;
	}

	public function dispose():Void {
		if (disposed)
			return;
		if (planOwner != null) {
			planOwner.close();
			planOwner = null;
		}
		executor.close();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "sceneRenderer has been disposed";
	}

	static function checkScene(status:Int, operation:String):Void {
		if (status != 0)
			throw '$operation failed with NativeKit scene status $status';
	}
}
