import compiler.Compiler;
import compiler.hl.HlWriter;
import compiler.runtime.CompilerIntrinsics;
import sys.io.File;

class HxiNativeKitSceneMain {
	static function main():Void {
		var output = Sys.args()[0],
			sceneHxi = File.getContent(Sys.args()[1]),
			renderHxi = File.getContent(Sys.args()[2]),
			interactionHxi = File.getContent(Sys.args()[3]),
			nativekitRoot = Sys.args()[4],
			nativekitHxi = File.getContent(Sys.args()[5]),
			gpuHxi = File.getContent(Sys.args()[6]),
			compiler = new Compiler();
		CompilerIntrinsics.register(compiler);
		compiler.addSourceRoot(Sys.getCwd() + "/stdlib");
		compiler.addSourceRoot(nativekitRoot + "/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/gpu/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/scene/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/scene_render/bindings/haxe");
		compiler.addSourceRoot(nativekitRoot + "/modules/scene_interaction/bindings/haxe");
		compiler.update("NativeKitWindow.hx", File.getContent(nativekitRoot + "/bindings/haxe/NativeKitWindow.hx"));
		compiler.addFfiProjection("NativeKit.hxmap", File.getContent(nativekitRoot + "/bindings/haxe/nativekit.hxmap"));
		compiler.addFfiProjection("NativeKitGpu.hxmap", File.getContent(nativekitRoot + "/modules/gpu/bindings/nativekit-gpu.hxmap"));
		compiler.addFfiProjection("NativeKitSceneRender.hxmap", File.getContent(nativekitRoot + "/modules/scene_render/bindings/nativekit-scene-render.hxmap"));
		compiler.addFfiProjection("NativeKitSceneInteraction.hxmap", File.getContent(nativekitRoot + "/modules/scene_interaction/bindings/nativekit-scene-interaction.hxmap"));
		compiler.addFfiInterface("NativeKit.hxi", nativekitHxi);
		compiler.addFfiInterface("NativeKitGpu.hxi", gpuHxi);
		compiler.addFfiInterface("NativeKitScene.hxi", sceneHxi);
		compiler.addFfiInterface("NativeKitSceneRender.hxi", renderHxi);
		compiler.addFfiInterface("NativeKitSceneInteraction.hxi", interactionHxi);
		compiler.update("Main.hx", source());
		File.saveBytes(output, HlWriter.encode(compiler.compile("Main").module));
	}

	static function source():String return '
import NativeKitScene;
import NativeKitSceneRender;
import NativeKitSceneInteraction;
import NativeKitGpu;
import NativeKit;
import NativeKit.WindowFlags;
import NativeKit.WindowKind;
import NativeKit.WindowOptions;
import NativeKitEventValue;
import NativeKitRuntime;
import nativekit.scene.Scene;
import nativekit.scene.SceneView;
import nativekit.scene.SceneViewPolicy;
import nativekit.scene.SourceEntityFilter;
import nativekit.scene.SceneRenderer;
import nativekit.scene.SpatialIndex;
import nativekit.scene.PickPollResult;
import nativekit.scene.PickResult;
import nativekit.scene.GeometryData;
import nativekit.scene.MaterialData;
import nativekit.scene.ImageData;
import nativekit.scene.TextureData;
import nativekit.scene.SamplerData;
import nativekit.scene.CameraData;
import nativekit.scene.LightData;
import nativekit.scene.Transform;
import nativekit.scene.TransformUpdate;
import nativekit.scene.Occurrence;
import nativekit.scene.VisibilityFilter;
import nativekit.scene.SelectionSet;
import nativekit.scene.SceneInteraction;
import nativekit.scene.ScenePresentation;
import nativekit.scene.SelectionMode;
import nativekit.gpu.Renderer;
import nativekit.gpu.Surface;
import haxe.io.Bytes;

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
		var image = scene.createImage(),
			pixels = Bytes.alloc(4);
		pixels.set(0, 255);
		pixels.set(1, 128);
		pixels.set(2, 64);
		pixels.set(3, 255);
		scene.setImageData(image, new ImageData(1, 1).setPixels(pixels));
		var texture = scene.createTexture();
		scene.setTextureData(texture, new TextureData(image));
		var sampler = scene.createSampler();
		scene.setSamplerData(sampler, new SamplerData());
		scene.setMaterialData(material, MaterialData.opaque(0.2, 0.7, 1.0)
			.setBaseColorTexture(texture, sampler));
		var highlight = scene.createMaterial();
		scene.setMaterialData(highlight, MaterialData.opaque(1.0, 0.8, 0.1));
		var hoverHighlight = scene.createMaterial();
		scene.setMaterialData(hoverHighlight, MaterialData.opaque(1.0, 0.2, 0.1));
		var camera = scene.createCamera();
		scene.setCameraData(camera, new CameraData().setPerspective(1.0, 0.01, 100.0, 1.0));
		var light = scene.createLight();
		scene.setLightData(light, new LightData().setIntensity(1.25));

		var transaction = scene.beginTransaction(),
			group = transaction.createOccurrence(),
			first = transaction.createOccurrence(),
			second = transaction.createOccurrence(),
			cameraOccurrence = transaction.createOccurrence(),
			lightOccurrence = transaction.createOccurrence();
		transaction.setParent(first, group);
		transaction.setParent(second, group);
		transaction.setName(group, "World");
		transaction.setEntityName(haxe.Int64.ofInt(42), "Panda");
		transaction.setGeometry(first, geometry);
		transaction.setGeometry(second, geometry);
		transaction.setMaterial(first, material);
		transaction.setMaterial(second, material);
		transaction.setSourceEntity(first, haxe.Int64.ofInt(42));
		transaction.setSourceEntity(second, haxe.Int64.ofInt(84));
		transaction.setCamera(cameraOccurrence, camera);
		transaction.setLight(lightOccurrence, light);
		transaction.setTransforms([
			new TransformUpdate(first, Transform.identity().translated(-0.65, 0.0, 0.0)),
			new TransformUpdate(second, Transform.identity().translated(0.65, 0.0, 0.0))
		]);
		var changes = transaction.commitWithChanges(),
			snapshot = scene.snapshot(),
			view = new SceneView().setRoot(group).setViewProjection(Transform.identity())
				.setCameraOccurrence(cameraOccurrence);
		var infos = snapshot.occurrences(),
			groupInfo = snapshot.find(group),
			firstInfo = snapshot.find(first),
			secondInfo = snapshot.find(second),
			children = snapshot.children(group);
		if (infos.length != 5 || groupInfo == null || firstInfo == null || secondInfo == null)
			return 10;
		var interaction = SceneInteraction.create();
		interaction.select(first, SelectionMode.Replace);
		if (!interaction.isSelected(first) || interaction.selected().length != 1)
			return 26;
		interaction.select(second, SelectionMode.Add);
		if (interaction.selected().length != 2)
			return 26;
		interaction.select(first, SelectionMode.Toggle);
		if (interaction.isSelected(first) || !interaction.isSelected(second))
			return 26;
		interaction.clearSelection();
		interaction.select(second, SelectionMode.Replace);
		var interactionView = new SceneView().setRoot(group);
		interaction.applySelection(interactionView, highlight);
		if (interaction.selectionSet().count() != 1
			|| interactionView.materialOverrideCount() != 1
			|| interactionView.selectionOverrideCount() != 1)
			return 27;
		interaction.select(first, SelectionMode.Add);
		interaction.applySelection(interactionView, highlight);
		if (interactionView.selectionOverrideCount() != 2)
			return 27;
		interaction.select(second, SelectionMode.Toggle);
		interaction.applySelection(interactionView, highlight);
		if (interactionView.selectionOverrideCount() != 1
			|| interactionView.materialOverrideCount() != 1
			|| interactionView.hoverOverrideCount() != 0)
			return 27;
		interaction.applyHover(interactionView, highlight);
		if (interactionView.selectionOverrideCount() != 1
			|| interactionView.hoverOverrideCount() != 0)
			return 27;
		interaction.dispose();
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
			|| materialView.selectionOverrideCount() != 1
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
		var subtreePolicy = new SceneViewPolicy().hideSubtree(group),
			subtreeView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.applyPolicy(subtreePolicy),
			subtreeExecution = sceneRenderer.render(snapshot, subtreeView),
			subtreeUpdate = sceneRenderer.lastUpdate();
		if (subtreePolicy.visibilityCount() != 1
			|| subtreeView.visibilityOverrideCount() != 1
			|| subtreeUpdate == null
			|| subtreeUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(subtreeUpdate.get_patched_visibility()) != 2
			|| haxe.Int64.toInt(subtreeExecution.get_commands()) != 0) return 29;
		var sourceFilter = new SourceEntityFilter()
			.hideSource(haxe.Int64.ofInt(84))
			.setSourceMaterial(haxe.Int64.ofInt(42), highlight),
			sourcePolicy = new SceneViewPolicy().applySourceFilter(snapshot, sourceFilter),
			sourceView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.applyPolicy(sourcePolicy),
			sourceExecution = sceneRenderer.render(snapshot, sourceView),
			sourceUpdate = sceneRenderer.lastUpdate();
		if (sourceFilter.visibilityRuleCount() != 1
			|| sourceFilter.materialRuleCount() != 1
			|| sourceView.visibilityOverrideCount() != 0
			|| sourceView.materialOverrideCount() != 0
			|| sourceView.sourceVisibilityOverrideCount() != 1
			|| sourceView.sourceMaterialOverrideCount() != 1
			|| sourceUpdate == null
			|| sourceUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(sourceUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(sourceUpdate.get_patched_materials()) != 1
			|| haxe.Int64.toInt(sourceUpdate.get_updated_geometry_resources()) != 0
			|| haxe.Int64.toInt(sourceUpdate.get_updated_material_resources()) != 0
			|| haxe.Int64.toInt(sourceExecution.get_commands()) != 1) return 30;
		var isolationFilter = new SourceEntityFilter().isolateSource(haxe.Int64.ofInt(42)),
			isolationPolicy = new SceneViewPolicy().applySourceFilter(snapshot, isolationFilter),
			isolationView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.applyPolicy(isolationPolicy),
			isolationRenderer = SceneRenderer.createHeadless(),
			isolationBaseView = new SceneView().setRoot(group).setViewProjection(Transform.identity()),
			isolationBaseExecution = isolationRenderer.render(snapshot, isolationBaseView),
			isolationExecution = isolationRenderer.render(snapshot, isolationView),
			isolationUpdate = isolationRenderer.lastUpdate();
		if (isolationFilter.isolationRuleCount() != 1
			|| isolationPolicy.isolationRuleCount() != 1
			|| isolationView.isolatedSourceCount() != 1
			|| isolationView.visibilityOverrideCount() != 0
			|| isolationView.sourceVisibilityOverrideCount() != 0
			|| isolationUpdate == null
			|| isolationUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(isolationUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(isolationExecution.get_commands()) != 1
			|| haxe.Int64.toInt(isolationBaseExecution.get_commands()) != 2) return 31;
		isolationRenderer.dispose();
		var presentationView = new SceneView().setRoot(group).setViewProjection(Transform.identity()),
			presentationRenderer = SceneRenderer.createHeadless(),
			sourcePresentation = ScenePresentation.create(presentationView, highlight, hoverHighlight);
		sourcePresentation.select(first, SelectionMode.Replace);
		sourcePresentation.hideSource(haxe.Int64.ofInt(84));
		var sourcePresentationExecution = sourcePresentation.render(presentationRenderer, snapshot),
			sourcePresentationUpdate = presentationRenderer.lastUpdate();
		if (presentationView.sourceVisibilityOverrideCount() != 1
			|| presentationView.isolatedSourceCount() != 0
			|| presentationView.selectionOverrideCount() != 1
			|| sourcePresentationUpdate != null
			|| haxe.Int64.toInt(sourcePresentationExecution.get_commands()) != 1) return 32;
		sourcePresentation.isolateSelection(snapshot);
		var selectedIsolationExecution = sourcePresentation.render(presentationRenderer, snapshot),
			selectedIsolationUpdate = presentationRenderer.lastUpdate();
		if (presentationView.isolatedSourceCount() != 1
			|| selectedIsolationUpdate == null
			|| selectedIsolationUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(selectedIsolationUpdate.get_patched_visibility()) != 0
			|| haxe.Int64.toInt(selectedIsolationExecution.get_commands()) != 1) return 32;
		sourcePresentation.clearIsolation();
		var clearedIsolationExecution = sourcePresentation.render(presentationRenderer, snapshot),
			clearedIsolationUpdate = presentationRenderer.lastUpdate();
		if (presentationView.isolatedSourceCount() != 0
			|| clearedIsolationUpdate == null
			|| clearedIsolationUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(clearedIsolationExecution.get_commands()) != 1) return 32;
		sourcePresentation.clearSourceFilter();
		var restoredPresentationExecution = sourcePresentation.render(presentationRenderer, snapshot),
			restoredPresentationUpdate = presentationRenderer.lastUpdate();
		if (presentationView.sourceVisibilityOverrideCount() != 0
			|| restoredPresentationUpdate == null
			|| restoredPresentationUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(restoredPresentationUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(restoredPresentationExecution.get_commands()) != 2) return 32;
		sourcePresentation.clearSelection();
		sourcePresentation.select(group, SelectionMode.Replace);
		sourcePresentation.isolateSelection(snapshot);
		var fallbackIsolationExecution = sourcePresentation.render(presentationRenderer, snapshot),
			fallbackIsolationUpdate = presentationRenderer.lastUpdate();
		if (presentationView.isolatedSourceCount() != 0
			|| presentationView.isolatedOccurrenceCount() != 1
			|| fallbackIsolationUpdate == null
			|| fallbackIsolationUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(fallbackIsolationExecution.get_commands()) != 2) return 33;
		sourcePresentation.clearIsolation();
		sourcePresentation.dispose();
		presentationRenderer.dispose();
		var clippedView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.addClipPlane(1.0, 0.0, 0.0, 0.0),
			clippedExecution = sceneRenderer.render(snapshot, clippedView),
			clippedUpdate = sceneRenderer.lastUpdate();
		if (clippedView.clipPlaneCount() != 1
			|| clippedUpdate == null
			|| clippedUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(clippedUpdate.get_patched_culling()) != 1
			|| haxe.Int64.toInt(clippedExecution.get_commands()) != 1) return 15;
		var visibilityPolicy = new SceneViewPolicy().hide(first).show(first).hide(first),
			materialPolicy = new SceneViewPolicy().setMaterial(second, material)
			.setMaterial(second, highlight),
			composedView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.applyPolicy(visibilityPolicy)
			.applyPolicy(materialPolicy)
			.addClipPlane(1.0, 0.0, 0.0, 0.6),
			composedExecution = sceneRenderer.render(snapshot, composedView),
			composedUpdate = sceneRenderer.lastUpdate();
		if (visibilityPolicy.visibilityCount() != 1
			|| materialPolicy.materialCount() != 1
			|| composedUpdate == null
			|| composedUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(composedUpdate.get_patched_visibility()) != 1
			|| haxe.Int64.toInt(composedUpdate.get_patched_materials()) != 1
			|| haxe.Int64.toInt(composedUpdate.get_patched_culling()) != 1
			|| haxe.Int64.toInt(composedExecution.get_commands()) != 1) return 16;
		view.setIncludeInvisible(true);

		var movedTransaction = scene.beginTransaction(),
			transform = Transform.identity().translated(-0.55, 0.0, 0.0);
		movedTransaction.setTransform(first, transform);
		var movedFrame = movedTransaction.commitFrame(),
			movedChanges = movedFrame.changeSet(),
			movedSnapshot = movedFrame.sceneSnapshot(),
			movedExecution = sceneRenderer.renderFrame(movedFrame, view),
			update = sceneRenderer.lastUpdate();
		var oldFirstInfo = snapshot.find(first),
			movedFirstInfo = movedSnapshot.find(first);
		if (oldFirstInfo == null || movedFirstInfo == null
			|| Math.abs(oldFirstInfo.worldTransform().element(12) + 0.65) > 0.0001
			|| Math.abs(movedFirstInfo.worldTransform().element(12) + 0.55) > 0.0001
			|| movedChanges == null
			|| update == null
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
		var presentation = ScenePresentation.create(view, highlight, hoverHighlight);
		presentation.select(second, SelectionMode.Replace);
		var presentationFrameTransaction = scene.beginTransaction(),
			presentationFrameChanges = presentationFrameTransaction.commitWithChanges(),
			presentationSnapshot = scene.snapshot();
		presentation.requestHover(realSceneRenderer, presentationSnapshot, 64, 64, 16, 32);
		var presentationExecution = presentation.render(realSceneRenderer, presentationSnapshot,
			presentationFrameChanges),
			hoverReady = presentation.interaction.hovered() != null;
		for (attempt in 0...100) {
			if (hoverReady)
				break;
			Sys.sleep(0.001);
			presentationExecution = presentation.render(realSceneRenderer, presentationSnapshot,
				presentationFrameChanges);
			hoverReady = presentation.interaction.hovered() != null;
		}
		var presentationUpdate = realSceneRenderer.lastUpdate();
		var presentationPatchedMaterials = presentationUpdate == null
			? -1 : haxe.Int64.toInt(presentationUpdate.get_patched_materials());
		presentationSnapshot.dispose();
		presentationFrameChanges.dispose();
		var nextFrameTransaction = scene.beginTransaction(),
			nextFrameChanges = nextFrameTransaction.commitWithChanges(),
			nextFrameSnapshot = scene.snapshot(),
			nextFrameExecution = presentation.render(realSceneRenderer, nextFrameSnapshot,
				nextFrameChanges),
			nextFrameUpdate = realSceneRenderer.lastUpdate();
		var validPresentationFrames = hoverReady
			&& presentation.view.selectionOverrideCount() == 1
			&& presentation.view.hoverOverrideCount() == 1
			&& presentation.view.materialOverrideCount() == 2
			&& presentationUpdate != null
			&& presentationUpdate.get_plan_rebuilt() == 0
			&& presentationPatchedMaterials >= 1
			&& presentationPatchedMaterials <= 2
			&& presentationExecution.get_result() == NativeKitGpu.GpuStatus.Ok
			&& haxe.Int64.toInt(presentationExecution.get_draw_calls()) == 2
			&& nextFrameUpdate != null
			&& nextFrameUpdate.get_plan_rebuilt() == 0
			&& haxe.Int64.toInt(nextFrameUpdate.get_patched_materials()) == 0
			&& nextFrameExecution.get_result() == NativeKitGpu.GpuStatus.Ok
			&& haxe.Int64.toInt(nextFrameExecution.get_draw_calls()) == 2;
		nextFrameChanges.dispose();
		presentation.dispose();
		if (!validPresentationFrames)
			return 28;
		var gpuClippedView = new SceneView().setRoot(group).setViewProjection(Transform.identity())
			.addClipPlane(1.0, 0.0, 0.0, 0.6),
			gpuClippedExecution = realSceneRenderer.render(nextFrameSnapshot, gpuClippedView),
			gpuClippedUpdate = realSceneRenderer.lastUpdate();
		if (gpuClippedUpdate == null
			|| gpuClippedUpdate.get_plan_rebuilt() != 0
			|| haxe.Int64.toInt(gpuClippedExecution.get_commands()) != 2
			|| haxe.Int64.toInt(gpuClippedExecution.get_draw_calls()) != 1) return 23;
		var clippedLeft = realSceneRenderer.pickPixel(nextFrameSnapshot, 64, 64, 8, 32),
			clippedRight = realSceneRenderer.pickPixel(nextFrameSnapshot, 64, 64, 16, 32);
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
		nextFrameSnapshot.dispose();
		culledSnapshot.dispose();
		restoredSnapshot.dispose();
		movedFrame.dispose();
		if (!movedFrame.isDisposed()
			|| !movedSnapshot.isDisposed()
			|| movedChanges == null
			|| !movedChanges.isDisposed()) return 34;
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
