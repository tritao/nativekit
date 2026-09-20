import compiler.Compiler;
import compiler.hl.HlWriter;
import compiler.runtime.CompilerIntrinsics;
import sys.io.File;

class HxiNativeKitSceneMain {
	static function main():Void {
		var output = Sys.args()[0],
			sceneHxi = File.getContent(Sys.args()[1]),
			renderHxi = File.getContent(Sys.args()[2]),
			nativekitRoot = Sys.args()[3],
			nativekitHxi = File.getContent(Sys.args()[4]),
			gpuHxi = File.getContent(Sys.args()[5]),
			compiler = new Compiler();
		CompilerIntrinsics.register(compiler);
		compiler.addSourceRoot(Sys.getCwd() + "/stdlib");
		compiler.addSourceRoot(nativekitRoot + "/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/gpu/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/scene/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/scene_render/bindings/haxe");
		compiler.update("NativeKitWindow.hx", File.getContent(nativekitRoot + "/bindings/haxe/NativeKitWindow.hx"));
		compiler.addFfiProjection("NativeKit.hxmap", File.getContent(nativekitRoot + "/bindings/haxe/nativekit.hxmap"));
		compiler.addFfiProjection("NativeKitGpu.hxmap", File.getContent(nativekitRoot + "/modules/gpu/bindings/nativekit-gpu.hxmap"));
		compiler.addFfiProjection("NativeKitSceneRender.hxmap", File.getContent(nativekitRoot + "/modules/scene_render/bindings/nativekit-scene-render.hxmap"));
		compiler.addFfiInterface("NativeKit.hxi", nativekitHxi);
		compiler.addFfiInterface("NativeKitGpu.hxi", gpuHxi);
		compiler.addFfiInterface("NativeKitScene.hxi", sceneHxi);
		compiler.addFfiInterface("NativeKitSceneRender.hxi", renderHxi);
		compiler.update("Main.hx", source());
		File.saveBytes(output, HlWriter.encode(compiler.compile("Main").module));
	}

	static function source():String return '
import NativeKitScene;
import NativeKitSceneRender;
import NativeKitGpu;
import NativeKit;
import NativeKit.WindowFlags;
import NativeKit.WindowKind;
import NativeKit.WindowOptions;
import NativeKitEventValue;
import NativeKitRuntime;
import nativekit.scene.Scene;
import nativekit.scene.SceneView;
import nativekit.scene.SceneRenderer;
import nativekit.scene.SpatialIndex;
import nativekit.scene.PickPollResult;
import nativekit.scene.PickResult;
import nativekit.scene.GeometryData;
import nativekit.scene.MaterialData;
import nativekit.scene.Transform;
import nativekit.scene.Occurrence;
import nativekit.scene.VisibilityFilter;
import nativekit.scene.SelectionSet;
import nativekit.gpu.Renderer;
import nativekit.gpu.Surface;

class Main {
	static function main():Int {
		var scene = Scene.create(),
			geometry = scene.createGeometry(),
			material = scene.createMaterial();

		var geometryData = new GeometryData();
		geometryData.addVertex(-0.6, -0.6, 0.0);
		geometryData.addVertex(0.6, -0.6, 0.0);
		geometryData.addVertex(0.0, 0.6, 0.0);
		geometryData.addTriangle(0, 1, 2);
		geometryData.setBounds(-0.6, -0.6, 0.0, 0.6, 0.6, 0.0);
		geometryData.addSubelement(0, 1, 42);
		scene.setGeometryData(geometry, geometryData);
		scene.setMaterialData(material, MaterialData.opaque(0.2, 0.7, 1.0));
		var highlight = scene.createMaterial();
		scene.setMaterialData(highlight, MaterialData.opaque(1.0, 0.8, 0.1));

		var transaction = scene.beginTransaction(),
			group = transaction.createOccurrence(),
			first = transaction.createOccurrence(),
			second = transaction.createOccurrence();
		transaction.setParent(first, group);
		transaction.setParent(second, group);
		transaction.setGeometry(first, geometry);
		transaction.setGeometry(second, geometry);
		transaction.setMaterial(first, material);
		transaction.setMaterial(second, material);
		transaction.setSourceEntity(first, haxe.Int64.ofInt(42));
		transaction.setSourceEntity(second, haxe.Int64.ofInt(84));
		transaction.setTransform(first, Transform.identity().translated(-0.65, 0.0, 0.0));
		transaction.setTransform(second, Transform.identity().translated(0.65, 0.0, 0.0));
		var changes = transaction.commitWithChanges(),
			snapshot = scene.snapshot(),
			view = new SceneView().setRoot(group).setViewProjection(Transform.identity());
		var infos = snapshot.occurrences(),
			groupInfo = snapshot.find(group),
			firstInfo = snapshot.find(first),
			secondInfo = snapshot.find(second),
			children = snapshot.children(group);
		if (infos.length != 3 || groupInfo == null || firstInfo == null || secondInfo == null)
			return 10;
		var spatialIndex = SpatialIndex.create(snapshot),
			spatialBounds = spatialIndex.queryBounds(-2.0, -1.0, -1.0, 0.0, 1.0, 1.0),
			spatialRay = spatialIndex.queryRay(-0.65, 0.0, 1.0, 0.0, 0.0, -1.0),
			spatialPick = spatialIndex.pickRay(-0.65, 0.0, 1.0, 0.0, 0.0, -1.0);
		if (spatialIndex.sourceRevision() != snapshot.revision()
			|| spatialBounds.length != 1
			|| !spatialBounds[0].equals(first)
			|| spatialRay.length != 1
			|| !spatialRay[0].equals(first)
			|| !spatialPick.occurrence().equals(first)
			|| spatialPick.sourceValue() != haxe.Int64.ofInt(42)
			|| spatialPick.subelement() != 42
			|| Math.abs(spatialPick.depth() - 1.0) > 0.0001) return 25;
		var firstParent = firstInfo.parent(),
			secondParent = secondInfo.parent();
		if (children.length != 2
			|| !children[0].equals(first)
			|| !children[1].equals(second)
			|| firstParent == null
			|| !firstParent.equals(group)
			|| secondParent == null
			|| !secondParent.equals(group)
			|| !firstInfo.visible()
			|| firstInfo.sourceValue() != haxe.Int64.ofInt(42)
			|| secondInfo.sourceValue() != haxe.Int64.ofInt(84)
			|| Math.abs(firstInfo.worldTransform().element(12) + 0.65) > 0.0001
			|| Math.abs(secondInfo.worldTransform().element(12) - 0.65) > 0.0001)
			return 10;

		var queryScene = Scene.create(),
			queryTransaction = queryScene.beginTransaction(),
			queryGroup = queryTransaction.createOccurrence(),
			queryLeaves:Array<Occurrence> = [];
		for (index in 0...49999) {
			var leaf = queryTransaction.createOccurrence();
			queryTransaction.setParent(leaf, queryGroup);
			queryLeaves.push(leaf);
		}
		queryTransaction.commit();
		var querySnapshot = queryScene.snapshot(),
			queryRevision = querySnapshot.revision(),
			queryStart = Sys.time(),
			queryInfos = querySnapshot.occurrences(),
			queryElapsed = Sys.time() - queryStart,
			queryLast = querySnapshot.find(queryLeaves[queryLeaves.length - 1]),
			queryChildren = querySnapshot.children(queryGroup);
		Sys.println("NativeKit scene query 50k: " + queryElapsed + "s");
		if (queryInfos.length != 50000
			|| queryLast == null
			|| queryChildren.length != 49999
			|| querySnapshot.revision() != queryRevision)
			return 9;
		querySnapshot.dispose();
		queryScene.dispose();

		var sceneRenderer = SceneRenderer.createHeadless(),
			execution = sceneRenderer.render(snapshot, view);
		if (haxe.Int64.toInt(execution.get_commands()) != 2
			|| haxe.Int64.toInt(execution.get_draw_calls()) != 2
			|| haxe.Int64.toInt(execution.get_geometry_resources_created()) != 1) return 11;
		var filter = new VisibilityFilter().hide(first),
			hiddenView = new SceneView().setRoot(group).applyVisibilityFilter(filter);
		var hiddenExecution = sceneRenderer.render(snapshot, hiddenView),
			hiddenUpdate = sceneRenderer.lastUpdate();
		if (filter.count() != 1
			|| hiddenView.visibilityOverrideCount() != 1
			|| hiddenUpdate == null
			|| hiddenUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(hiddenUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(hiddenExecution.get_commands()) != 1) return 12;
		var selection = new SelectionSet().add(second),
			materialView = new SceneView().setRoot(group).applySelection(selection, highlight);
		var materialExecution = sceneRenderer.render(snapshot, materialView),
			materialUpdate = sceneRenderer.lastUpdate();
		if (selection.count() != 1
			|| materialView.materialOverrideCount() != 1
			|| materialUpdate == null
			|| materialUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(materialUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(materialUpdate.get_patched_materials()) != 1
			|| haxe.Int64.toInt(materialExecution.get_commands()) != 2) return 13;
		var refreshView = new SceneView().setRoot(group).setViewProjection(Transform.identity());
		refreshView.setIncludeInvisible(true);
		var refreshedExecution = sceneRenderer.render(snapshot, refreshView),
			refreshed = sceneRenderer.lastUpdate();
		if (refreshed == null || refreshed.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(refreshed.get_patched_materials()) != 1
			|| haxe.Int64.toInt(refreshedExecution.get_commands()) != 2) return 14;
		var clippedView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.addClipPlane(1.0, 0.0, 0.0, 0.0),
			clippedExecution = sceneRenderer.render(snapshot, clippedView),
			clippedUpdate = sceneRenderer.lastUpdate();
		if (clippedView.clipPlaneCount() != 1
			|| clippedUpdate == null
			|| clippedUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(clippedUpdate.get_patched_culling()) != 1
			|| haxe.Int64.toInt(clippedExecution.get_commands()) != 1) return 15;
		var composedView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.setVisibility(first, false)
			.setMaterial(second, highlight)
			.addClipPlane(1.0, 0.0, 0.0, 0.6),
			composedExecution = sceneRenderer.render(snapshot, composedView),
			composedUpdate = sceneRenderer.lastUpdate();
		if (composedUpdate == null
			|| composedUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(composedUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(composedUpdate.get_patched_materials()) != 1
			|| haxe.Int64.toInt(composedUpdate.get_patched_culling()) != 1
			|| haxe.Int64.toInt(composedExecution.get_commands()) != 1) return 16;
		view.setIncludeInvisible(true);

		var movedTransaction = scene.beginTransaction(),
			transform = Transform.identity().translated(-0.55, 0.0, 0.0);
		movedTransaction.setTransform(first, transform);
		var movedChanges = movedTransaction.commitWithChanges(),
			movedSnapshot = scene.snapshot(),
			movedExecution = sceneRenderer.render(movedSnapshot, view, movedChanges),
			update = sceneRenderer.lastUpdate();
		if (update == null
			|| update.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(update.get_patched_instances()) != 1
			|| haxe.Int64.toInt(update.get_updated_geometry_resources()) != 0
			|| haxe.Int64.toInt(movedExecution.get_commands()) != 2) return 17;
		var culledTransaction = scene.beginTransaction();
		culledTransaction.setTransform(first, Transform.identity().translated(2.0, 0.0, 0.0));
		var culledChanges = culledTransaction.commitWithChanges(),
			culledSnapshot = scene.snapshot(),
			culledExecution = sceneRenderer.render(culledSnapshot, view, culledChanges),
			culledUpdate = sceneRenderer.lastUpdate();
		if (culledUpdate == null
			|| culledUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(culledUpdate.get_patched_instances()) != 1
			|| haxe.Int64.toInt(culledUpdate.get_patched_culling()) != 1
			|| haxe.Int64.toInt(culledUpdate.get_visible_items()) != 1
			|| haxe.Int64.toInt(culledUpdate.get_culled_items()) != 1
			|| haxe.Int64.toInt(culledExecution.get_commands()) != 1) return 22;
		var restoredTransaction = scene.beginTransaction();
		restoredTransaction.setTransform(first, Transform.identity().translated(-0.55, 0.0, 0.0));
		var restoredChanges = restoredTransaction.commitWithChanges(),
			restoredSnapshot = scene.snapshot();
		sceneRenderer.render(restoredSnapshot, view, restoredChanges);

		var realRuntime = NativeKitRuntime.start(),
			windowOptions = new WindowOptions();
		windowOptions.set_width(64);
		windowOptions.set_height(64);
		windowOptions.set_title("Haxeon: NativeKit scene renderer");
		windowOptions.set_flags(WindowFlags.Resizable);
		windowOptions.set_owner(NativeKit.WindowHandle.invalid());
		windowOptions.set_kind(WindowKind.Normal);
		var window = realRuntime.createWindow(windowOptions),
			surface = Surface.create(window, 64, 64),
			ready = false,
			subscription = realRuntime.events.listen(function(value) switch value {
				case SurfaceReady(source) if (source.rawValue() == surface.nativeHandle().rawValue()):
					ready = true;
				case _:
			});
		var deadline = Sys.time() + 5.0;
		while (!ready && Sys.time() < deadline)
			realRuntime.events.poll();
		if (!ready) return 18;
		var renderer:Renderer = surface.createRenderer(),
			realSceneRenderer = SceneRenderer.create(renderer),
			realExecution = realSceneRenderer.render(snapshot, view);
		if (realExecution.get_result() != NativeKitGpu.GpuStatus.Ok
			|| haxe.Int64.toInt(realExecution.get_commands()) != 2
			|| haxe.Int64.toInt(realExecution.get_draw_calls()) != 1
			|| haxe.Int64.toInt(realExecution.get_geometry_resources_created()) != 1) return 19;
		var realMovedExecution = realSceneRenderer.render(movedSnapshot, view, movedChanges);
		if (realMovedExecution.get_result() != NativeKitGpu.GpuStatus.Ok
			|| haxe.Int64.toInt(realMovedExecution.get_commands()) != 2
			|| haxe.Int64.toInt(realMovedExecution.get_draw_calls()) != 1
			|| haxe.Int64.toInt(realMovedExecution.get_geometry_resources_created()) != 0
			|| haxe.Int64.toInt(realMovedExecution.get_geometry_resources_updated()) != 0
			|| haxe.Int64.toInt(realMovedExecution.get_instance_records_updated()) != 1) return 20;
		var pickedFirst = realSceneRenderer.pickPixel(movedSnapshot, 64, 64, 16, 32),
			pickedSecond = realSceneRenderer.pickPixel(movedSnapshot, 64, 64, 48, 32);
		if (!pickedFirst.occurrence().equals(first)
			|| !pickedSecond.occurrence().equals(second)
			|| pickedFirst.sourceValue() != haxe.Int64.ofInt(42)
			|| pickedSecond.sourceValue() != haxe.Int64.ofInt(84)
			|| pickedFirst.subelement() != 42
			|| pickedSecond.subelement() != 42) {
			return 21;
		}
		var asyncPickRequest = realSceneRenderer.pickPixelAsync(movedSnapshot, 64, 64, 16, 32),
			asyncPick:Null<PickResult> = null;
		for (attempt in 0...100) {
			switch (asyncPickRequest.poll(movedSnapshot)) {
				case Pending:
					Sys.sleep(0.001);
				case Ready(value):
					asyncPick = value;
					break;
				case Stale:
					asyncPickRequest.dispose();
					return 22;
				case Failed(_):
					asyncPickRequest.dispose();
					return 22;
			}
		}
		if (asyncPick == null
			|| !asyncPick.occurrence().equals(first)
			|| asyncPick.sourceValue() != haxe.Int64.ofInt(42)
			|| asyncPick.subelement() != 42
			|| Math.abs(asyncPick.worldX() + 0.484375) > 0.05
			|| Math.abs(asyncPick.worldZ()) > 0.001
			|| Math.abs(asyncPick.depth() - 0.5) > 0.01) {
			asyncPickRequest.dispose();
			return 22;
		}
		asyncPickRequest.dispose();
		var gpuClippedView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.addClipPlane(1.0, 0.0, 0.0, 0.6),
			gpuClippedExecution = realSceneRenderer.render(movedSnapshot, gpuClippedView),
			gpuClippedUpdate = realSceneRenderer.lastUpdate();
		if (gpuClippedUpdate == null
			|| gpuClippedUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(gpuClippedExecution.get_commands()) != 2
			|| haxe.Int64.toInt(gpuClippedExecution.get_draw_calls()) != 1) return 23;
		var clippedLeft = realSceneRenderer.pickPixel(movedSnapshot, 64, 64, 8, 32),
			clippedRight = realSceneRenderer.pickPixel(movedSnapshot, 64, 64, 16, 32);
		if (clippedLeft.occurrence().stableValue() != haxe.Int64.ofInt(0)
			|| !clippedRight.occurrence().equals(first)
			|| clippedRight.subelement() != 42) return 24;
		realSceneRenderer.dispose();
		subscription.dispose();
		renderer.dispose();
		surface.dispose();
		realRuntime.dispose();

		sceneRenderer.dispose();
		spatialIndex.dispose();
		movedSnapshot.dispose();
		culledSnapshot.dispose();
		restoredSnapshot.dispose();
		movedChanges.dispose();
		culledChanges.dispose();
		restoredChanges.dispose();
		snapshot.dispose();
		changes.dispose();
		scene.dispose();
		return 42;
	}
}
';
}
