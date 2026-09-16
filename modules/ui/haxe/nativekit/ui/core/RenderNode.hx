package nativekit.ui.core;

import LayoutNode;
import LayoutStyle;
import LayoutVisualKind;
import Canvas;
import CompositeMode;
import ResolvedLayoutItem;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.Decoration;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.semantics.Semantics;

/** One Haxe-owned node joins visual layout, interaction, focus, and state identity. */
class RenderNode {
	public final id:WidgetId;
	public final layout:LayoutNode;
	public final children:Array<RenderNode>;
	public var parent(default, null):Null<RenderNode>;
	public var resolved:Null<ResolvedLayoutItem>;
	public var focusable:Bool;
	public var focusTrap:Bool;
	public var hitTestSelf:Bool;
	public var enabled:Bool;
	/** Generic pseudo-state flags maintained by the routed interaction system. */
	public var states:Int;
	public var styleType:Null<String>;
	public var styleKey:Null<String>;
	public var styleId:Null<String>;
	public var styleClasses:Array<String>;
	public var styleTags:Array<String>;
	public var computedStyle:Null<ComputedStyle>;
	public var tabIndex:Int;
	/** Cursor intent used while this node is hovered or holds pointer capture. */
	public var cursor:Null<CursorShape>;
	public var semantics:Null<Semantics>;
	final handlers:Map<String, Array<UiEvent->Void>>;
	final outsidePointerDownHandlers:Array<UiEvent->Void>;
	final resolvedHandlers:Array<ResolvedLayoutItem->Void>;
	final paintHandlers:Array<Canvas->ResolvedLayoutItem->Void>;
	final paintCacheKeys:Array<Null<String>>;
	final decorations:Array<Decoration>;
	final decorationCacheKeys:Array<Null<String>>;

	public function new(id:WidgetId, kind:LayoutVisualKind = LayoutVisualKind.Box, ?style:LayoutStyle) {
		if (id == null)
			throw "Render nodes require a stable widget ID";
		this.id = id;
		layout = new LayoutNode(id.value, kind, style);
		children = [];
		parent = null;
		resolved = null;
		focusable = false;
		focusTrap = false;
		hitTestSelf = true;
		enabled = true;
		states = 0;
		styleType = null;
		styleKey = null;
		styleId = null;
		styleClasses = [];
		styleTags = [];
		computedStyle = null;
		tabIndex = 0;
		cursor = null;
		semantics = null;
		handlers = new Map();
		outsidePointerDownHandlers = [];
		resolvedHandlers = [];
		paintHandlers = [];
		paintCacheKeys = [];
		decorations = [];
		decorationCacheKeys = [];
	}

	/** Publishes the typed selector identity associated with this render node. */
	public function setStyleIdentity(type:String, ?key:String, ?id:String,
			?classes:Array<String>, ?tags:Array<String>):RenderNode {
		if (type == null || type.length == 0)
			throw "Styled render nodes require a type";
		styleType = type;
		styleKey = key;
		styleId = id;
		styleClasses = classes == null ? [] : classes.copy();
		styleTags = tags == null ? [] : tags.copy();
		return this;
	}

	public function add(child:RenderNode):RenderNode {
		if (child == null || child == this || child.parent != null)
			throw "A render node must have one parent and cannot contain itself";
		for (ancestor in ancestors())
			if (ancestor == child)
				throw "Render tree contains a cycle";
		child.parent = this;
		children.push(child);
		layout.add(child.layout);
		return child;
	}

	/** Copies resolved typography onto this node's concrete layout payload. */
	public function applyTextStyle(style:ResolvedTextStyle):RenderNode {
		if (style == null)
			throw "Render text styles cannot be null";
		layout.textColor = style.textColor;
		layout.textStyle.font = style.textStyle.font;
		layout.textStyle.fontSize = style.textStyle.fontSize;
		layout.textStyle.letterSpacing = style.textStyle.letterSpacing;
		layout.paragraphStyle.wrap = style.paragraphStyle.wrap;
		layout.paragraphStyle.alignment = style.paragraphStyle.alignment;
		layout.paragraphStyle.lineHeight = style.paragraphStyle.lineHeight;
		layout.paragraphStyle.direction = style.paragraphStyle.direction;
		return this;
	}

	public function on(kind:String, handler:UiEvent->Void, phase:String = "bubble"):RenderNode {
		if (kind == null || kind.length == 0 || handler == null ||
			(phase != "capture" && phase != "target" && phase != "bubble"))
			throw "Event handlers require a kind and callback";
		var key = handlerKey(kind, phase);
		var values = handlers.get(key);
		if (values == null) {
			values = [];
			handlers.set(key, values);
		}
		values.push(handler);
		return this;
	}

	/** Runs when a pointer press targets a node outside this node's subtree. */
	public function onPointerDownOutside(handler:UiEvent->Void):RenderNode {
		if (handler == null)
			throw "Outside pointer handlers require a callback";
		outsidePointerDownHandlers.push(handler);
		return this;
	}

	public function onResolved(handler:ResolvedLayoutItem->Void):RenderNode {
		if (handler == null)
			throw "Resolved geometry handlers cannot be null";
		resolvedHandlers.push(handler);
		return this;
	}

	/**
	 * Adds a custom paint callback. A cache key opts the callback into retained
	 * display-list reuse; it must change whenever the callback's output can
	 * change for reasons other than geometry or computed style.
	 */
	public function onPaint(handler:Canvas->ResolvedLayoutItem->Void,
			?cacheKey:String):RenderNode {
		if (layout.visualKind != LayoutVisualKind.Custom)
			throw "Custom paint handlers require a Custom render node";
		if (handler == null)
			throw "Render paint handlers cannot be null";
		if (cacheKey != null && cacheKey.length == 0)
			throw "Render paint cache keys cannot be empty";
		paintHandlers.push(handler);
		paintCacheKeys.push(cacheKey);
		return this;
	}

	/**
	 * Adds a decoration. Supplying a cache key opts it into retained display
	 * list reuse; the key must include any external inputs read by the
	 * decoration that are not represented by computed style or geometry.
	 */
	public function addDecoration(decoration:Decoration, ?cacheKey:String):RenderNode {
		if (layout.visualKind != LayoutVisualKind.Custom)
			throw "Render decorations require a Custom render node";
		if (decoration == null)
			throw "Render decorations cannot be null";
		if (cacheKey != null && cacheKey.length == 0)
			throw "Render decoration cache keys cannot be empty";
		decorations.push(decoration);
		decorationCacheKeys.push(cacheKey);
		return this;
	}

	@:allow(nativekit.ui.core.UiContext)
	function hasPaintHandler():Bool
		return paintHandlers.length > 0 || decorations.length > 0;

	/** Returns the complete opt-in fingerprint for safe retained paint reuse. */
	@:allow(nativekit.ui.core.UiContext)
	function retainedPaintKey():Null<String> {
		if (!hasPaintHandler())
			return null;
		var result = "paint";
		for (index in 0...paintHandlers.length) {
			var key = paintCacheKeys[index];
			if (key == null)
				return null;
			result += "|handler:" + key;
		}
		for (index in 0...decorations.length) {
			var key = decorationCacheKeys[index];
			if (key == null)
				return null;
			result += "|decoration:" + key;
		}
		return result;
	}

	@:allow(nativekit.ui.core.UiContext)
	function setResolved(item:Null<ResolvedLayoutItem>):Void {
		resolved = item;
		if (item != null)
			for (handler in resolvedHandlers)
				handler(item);
	}

	@:allow(nativekit.ui.core.UiContext)
	function paint(canvas:Canvas):Bool {
		if (resolved == null || !resolved.visible ||
			(paintHandlers.length == 0 && decorations.length == 0))
			return false;
		var style = computedStyle == null ? new ComputedStyle() : computedStyle;
		var paintContent:Canvas->Void = function(target:Canvas) {
			for (decoration in decorations)
				decoration.paint(target, resolved, style);
			for (handler in paintHandlers)
				handler(target, resolved);
		};
		var opacity = style.get(StyleProperty.Opacity);
		var effects = style.get(StyleProperty.Effects);
		var hasEffects = effects != null && effects.effects.length > 0;
		var backdropEffects = style.get(StyleProperty.BackdropEffects);
		var hasBackdropEffects = backdropEffects != null && backdropEffects.effects.length > 0;
		var mask = style.get(StyleProperty.Mask);
		if (opacity < 1.0 || hasEffects || hasBackdropEffects || mask != null)
			canvas.withLayer(opacity, paintContent, CompositeMode.SourceOver, resolved.bounds(),
				hasEffects ? effects : null, mask, hasBackdropEffects ? backdropEffects : null);
		else
			paintContent(canvas);
		return true;
	}

	@:allow(nativekit.ui.core.EventDispatcher)
	function invoke(event:UiEvent):Void {
		if (event.phase == "target") {
			invokePhase(event, "target");
			if (!event.immediatePropagationStopped)
				invokePhase(event, "bubble");
		} else
			invokePhase(event, event.phase);
	}

	@:allow(nativekit.ui.core.EventDispatcher)
	function invokePointerDownOutside(event:UiEvent):Void {
		for (handler in outsidePointerDownHandlers) {
			event.currentTarget = id;
			event.phase = "outside";
			handler(event);
			if (event.propagationStopped)
				return;
		}
	}

	function invokePhase(event:UiEvent, phase:String):Void {
		var values = handlers.get(handlerKey(event.kind, phase));
		if (values == null)
			return;
		for (handler in values) {
			handler(event);
			if (event.immediatePropagationStopped)
				return;
		}
	}

	static inline function handlerKey(kind:String, phase:String):String
		return kind + "#" + phase;

	public function find(id:WidgetId):Null<RenderNode> {
		if (id == null)
			return null;
		var pending:Array<RenderNode> = [this];
		var visited:Array<RenderNode> = [];
		while (pending.length > 0) {
			var node = pending.pop();
			if (node == null)
				continue;
			var alreadyVisited = false;
			for (visitedNode in visited)
				if (visitedNode == node) {
					alreadyVisited = true;
					break;
				}
			if (alreadyVisited)
				continue;
			visited.push(node);
			if (node.id.equals(id))
				return node;
			var index = node.children.length - 1;
			while (index >= 0) {
				var child = node.children[index];
				if (child != null)
					pending.push(child);
				index--;
			}
		}
		return null;
	}

	public function walk(visit:RenderNode->Void):Void {
		visit(this);
		for (child in children)
			child.walk(visit);
	}

	function ancestors():Array<RenderNode> {
		var result:Array<RenderNode> = [];
		var node = parent;
		while (node != null) {
			var present:RenderNode = cast node;
			result.push(present);
			node = present.parent;
		}
		return result;
	}
}
