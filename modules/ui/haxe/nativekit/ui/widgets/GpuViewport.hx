package nativekit.ui.widgets;

import Canvas;
import Color;
import GraphicsSurface;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import ResolvedLayoutItem;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CachePolicy;
import nativekit.ui.core.GraphicsSurfaceViewportContent;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.core.ViewportCamera;
import nativekit.ui.core.ViewportContent;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleTarget;

/**
 * Shared editor viewport node for GPU-produced content.
 *
 * The GPU renderer remains application-owned; this node owns compositing,
 * camera navigation, retained invalidation, and pointer interaction. That
 * keeps simulation/CAD renderers independent from the UI tree.
 */
class GpuViewport implements View {
	public final key:String;
	public final content:ViewportContent;
	public final camera:ViewportCamera;
	public final style:LayoutStyle;
	public var background(default, null):Color;
	public var gridColor(default, null):Color;
	public var gridSize(default, null):Float;
	public var gridEnabled(default, null):Bool;
	public var enabled:Bool;
	public var label:Null<String>;
	public var overlay(default, null):Null<Canvas->ResolvedLayoutItem->Void>;
	var overlayRevision:Int;
	final handlers:Map<String, Array<UiEvent->Void>>;
	var draggingPointer:Null<Int>;
	var lastPointerX:Float;
	var lastPointerY:Float;

	public function new(key:String, content:ViewportContent, ?camera:ViewportCamera,
			?style:LayoutStyle, ?label:String) {
		if (key == null || key.length == 0 || content == null)
			throw "GPU viewports require a stable key and content";
		this.key = key;
		this.content = content;
		this.camera = camera == null ? new ViewportCamera() : camera;
		this.style = style == null ? defaultStyle() : style.copy();
		this.background = Color.rgba(0.055, 0.065, 0.08, 1.0);
		this.gridColor = Color.rgba(0.18, 0.21, 0.26, 0.7);
		this.gridSize = 1.0;
		this.gridEnabled = true;
		this.enabled = true;
		this.label = label == null ? "GPU viewport" : label;
		this.overlay = null;
		overlayRevision = 1;
		handlers = new Map();
		draggingPointer = null;
		lastPointerX = 0.0;
		lastPointerY = 0.0;
	}

	public static function fromSurface(key:String, surface:GraphicsSurface, width:Float,
			height:Float, ?camera:ViewportCamera, ?style:LayoutStyle, ?label:String):GpuViewport
		return new GpuViewport(key, new GraphicsSurfaceViewportContent(surface, width, height),
			camera, style, label);

	public function on(kind:String, handler:UiEvent->Void):GpuViewport {
		if (kind == null || kind.length == 0 || handler == null)
			throw "GPU viewport handlers require an event kind and callback";
		var values = handlers.get(kind);
		if (values == null) {
			values = [];
			handlers.set(kind, values);
		}
		values.push(handler);
		return this;
	}

	public function fit(width:Float, height:Float, margin:Float = 16.0):Bool
		return camera.fit(content.width(), content.height(), width, height, margin);

	public function setAppearance(background:Color, gridColor:Color, gridSize:Float = 1.0,
			gridEnabled:Bool = true):Void {
		if (background == null || gridColor == null || !finite(gridSize) || gridSize <= 0.0)
			throw "Viewport appearance values are invalid";
		this.background = background;
		this.gridColor = gridColor;
		this.gridSize = gridSize;
		this.gridEnabled = gridEnabled;
	}

	public function setOverlay(overlay:Null<Canvas->ResolvedLayoutItem->Void>):Void {
		this.overlay = overlay;
		overlayRevision++;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("viewport");
			var computed = context.resolveStyle(new StyleTarget("gpu-viewport", key, key,
				null, ["gpu-viewport"], context.interactionStates.get(nodeId)), style);
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("gpu-viewport", key, key, null, ["gpu-viewport"]);
			node.states = context.interactionStates.get(nodeId);
			node.enabled = enabled;
			node.hitTestSelf = enabled;
			node.cachePolicy = CachePolicy.Raster;
			node.computedStyle = computed;
			node.semantics = new Semantics(AccessibilityRole.Group,
				label == null ? "GPU viewport" : label);
			var cacheKey = "viewport:" + content.revision() + ":camera:" + camera.revision +
				":appearance:" + appearanceKey() + ":overlay:" + overlayRevision;
			node.onPaint(function(canvas, geometry) paint(canvas, geometry), cacheKey);
			node.on(UiEventKind.PointerDown, function(event) {
				if (!enabled || event.button != 0)
					return;
				draggingPointer = event.pointerId;
				lastPointerX = event.localX;
				lastPointerY = event.localY;
				event.capturePointer();
				event.stopPropagation();
			});
			node.on(UiEventKind.PointerMove, function(event) {
				if (draggingPointer == null || event.pointerId != draggingPointer)
					return;
				camera.panByScreen(event.localX - lastPointerX, event.localY - lastPointerY);
				lastPointerX = event.localX;
				lastPointerY = event.localY;
				context.commands.refresh();
			});
			node.on(UiEventKind.PointerUp, finishPointer);
			node.on(UiEventKind.PointerCancel, finishPointer);
			node.on(UiEventKind.Scroll, function(event) {
				if (!enabled || event.deltaY == 0.0)
					return;
				camera.zoomAt(Math.pow(1.1, -event.deltaY / 100.0), event.localX, event.localY);
				context.commands.refresh();
				event.stopPropagation();
			});
			for (kind in handlers.keys())
				for (handler in handlers.get(kind))
					node.on(kind, handler);
			return node;
		});
	}

	function finishPointer(event:UiEvent):Void {
		if (draggingPointer == null || event.pointerId != draggingPointer)
			return;
		draggingPointer = null;
		event.releasePointer();
	}

	function paint(canvas:Canvas, geometry:ResolvedLayoutItem):Void {
		canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height), background);
		canvas.withState(function(viewCanvas) {
			viewCanvas.translate(-camera.panX * camera.zoom, -camera.panY * camera.zoom);
			viewCanvas.scale(camera.zoom, camera.zoom);
			if (gridEnabled)
				paintGrid(viewCanvas, geometry);
			content.paint(viewCanvas, new Rect(0.0, 0.0, content.width(), content.height()));
		});
		if (overlay != null)
			overlay(canvas, geometry);
	}

	function paintGrid(canvas:Canvas, geometry:ResolvedLayoutItem):Void {
		if (gridSize <= 0.0 || !finite(gridSize))
			return;
		var topLeft = camera.viewportToWorld(0.0, 0.0);
		var bottomRight = camera.viewportToWorld(geometry.width, geometry.height);
		var startX = Math.floor(topLeft.x / gridSize) * gridSize;
		var startY = Math.floor(topLeft.y / gridSize) * gridSize;
		var endX = Math.ceil(bottomRight.x / gridSize) * gridSize;
		var endY = Math.ceil(bottomRight.y / gridSize) * gridSize;
		var path = new PathBuilder();
		var count = 0;
		var x = startX;
		while (x <= endX && count < 512) {
			path.moveTo(x, startY).lineTo(x, endY);
			x += gridSize;
			count++;
		}
		var y = startY;
		count = 0;
		while (y <= endY && count < 512) {
			path.moveTo(startX, y).lineTo(endX, y);
			y += gridSize;
			count++;
		}
		if (x != startX || y != startY)
			canvas.strokeTransient(path.build(), gridColor, 1.0 / camera.zoom,
				LineCap.Butt, LineJoin.Miter);
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.stretch();
		result.height = LayoutAxis.stretch();
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	function appearanceKey():String
		return background.red + ":" + background.green + ":" + background.blue + ":" + background.alpha +
			":" + gridColor.red + ":" + gridColor.green + ":" + gridColor.blue + ":" + gridColor.alpha +
			":" + gridSize + ":" + (gridEnabled ? "1" : "0");
}
