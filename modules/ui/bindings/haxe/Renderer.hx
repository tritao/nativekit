import NativeKit;
import NativeKitUI;

/** Typed renderer facade for retained UI display lists. */
class Renderer {
	var value:nkui_renderer;
	var disposed:Bool;

	private function new(value:nkui_renderer) {
		this.value = value;
		disposed = false;
	}

	public static function create():Renderer {
		var made = NativeKitUI.nkui_renderer_create();
		UiResult.check(made.status, "renderer.create");
		return new Renderer(made.out_renderer);
	}

	public function render(list:DisplayList, surface:Surface):Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_renderer_render(value, list.nativeHandle(), surface.nativeHandle()), "renderer.render");
	}

	public function renderFrame(list:DisplayList, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_renderer_render_frame(value, list.nativeHandle(), surface.nativeHandle(), frame.nativeValue()), "renderer.renderFrame");
	}

	public function stats():RendererStats {
		ensureLive();
		var result = NativeKitUI.nkui_renderer_get_stats(value);
		UiResult.check(result.status, "renderer.stats");
		var stats = result.out_stats;
		return new RendererStats(stats.get_path_preparations(), stats.get_path_cache_hits(), stats.get_path_cache_misses(),
			stats.get_path_vertices_generated(), stats.get_path_geometry_bytes_allocated(), stats.get_path_tessellation_nanoseconds(),
			stats.get_path_geometry_bytes_retained());
	}

	/** Releases the renderer and its backend resources. Repeated disposal is safe. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = NativeKitUI.nkui_renderer_destroy(value);
		disposed = true;
		UiResult.check(status, "renderer.dispose");
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Renderer has been disposed";
	}
}

/** Immutable renderer preparation counters. */
class RendererStats {
	public final pathPreparations:haxe.Int64;
	public final pathCacheHits:haxe.Int64;
	public final pathCacheMisses:haxe.Int64;
	public final pathVerticesGenerated:haxe.Int64;
	public final pathGeometryBytesAllocated:haxe.Int64;
	public final pathTessellationNanoseconds:haxe.Int64;
	public final pathGeometryBytesRetained:haxe.Int64;

	public function new(pathPreparations:haxe.Int64, pathCacheHits:haxe.Int64, pathCacheMisses:haxe.Int64, pathVerticesGenerated:haxe.Int64,
		pathGeometryBytesAllocated:haxe.Int64, pathTessellationNanoseconds:haxe.Int64, pathGeometryBytesRetained:haxe.Int64) {
		this.pathPreparations = pathPreparations;
		this.pathCacheHits = pathCacheHits;
		this.pathCacheMisses = pathCacheMisses;
		this.pathVerticesGenerated = pathVerticesGenerated;
		this.pathGeometryBytesAllocated = pathGeometryBytesAllocated;
		this.pathTessellationNanoseconds = pathTessellationNanoseconds;
		this.pathGeometryBytesRetained = pathGeometryBytesRetained;
	}
}
