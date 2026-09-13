package nativekit.ui.core;

import LayoutNode;
import LayoutStyle;
import LayoutVisualKind;
import Canvas;
import ResolvedLayoutItem;
import nativekit.ui.semantics.Semantics;

/** One Haxe-owned node joins visual layout, interaction, focus, and state identity. */
class RenderNode {
	public final id:WidgetId;
	public final layout:LayoutNode;
	public final children:Array<RenderNode>;
	public var parent(default, null):Null<RenderNode>;
	public var resolved:Null<ResolvedLayoutItem>;
	public var focusable:Bool;
	public var enabled:Bool;
	public var tabIndex:Int;
	public var semantics:Null<Semantics>;
	final handlers:Map<String, Array<UiEvent->Void>>;
	final resolvedHandlers:Array<ResolvedLayoutItem->Void>;
	final paintHandlers:Array<Canvas->ResolvedLayoutItem->Void>;

	public function new(id:WidgetId, kind:LayoutVisualKind = LayoutVisualKind.Box, ?style:LayoutStyle) {
		if (id == null)
			throw "Render nodes require a stable widget ID";
		this.id = id;
		layout = new LayoutNode(id.value, kind, style);
		children = [];
		parent = null;
		resolved = null;
		focusable = false;
		enabled = true;
		tabIndex = 0;
		semantics = null;
		handlers = new Map();
		resolvedHandlers = [];
		paintHandlers = [];
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

	public function onResolved(handler:ResolvedLayoutItem->Void):RenderNode {
		if (handler == null)
			throw "Resolved geometry handlers cannot be null";
		resolvedHandlers.push(handler);
		return this;
	}

	public function onPaint(handler:Canvas->ResolvedLayoutItem->Void):RenderNode {
		if (handler == null)
			throw "Render paint handlers cannot be null";
		paintHandlers.push(handler);
		return this;
	}

	@:allow(nativekit.ui.core.UiContext)
	function hasPaintHandler():Bool
		return paintHandlers.length > 0;

	@:allow(nativekit.ui.core.UiContext)
	function setResolved(item:Null<ResolvedLayoutItem>):Void {
		resolved = item;
		if (item != null)
			for (handler in resolvedHandlers)
				handler(item);
	}

	@:allow(nativekit.ui.core.UiContext)
	function paint(canvas:Canvas):Bool {
		if (resolved == null || !resolved.visible || paintHandlers.length == 0)
			return false;
		for (handler in paintHandlers)
			handler(canvas, resolved);
		return true;
	}

	@:allow(nativekit.ui.core.EventDispatcher)
	function invoke(event:UiEvent):Void {
		if (event.phase == "target") {
			invokePhase(event, "target");
			if (!event.propagationStopped)
				invokePhase(event, "bubble");
		} else
			invokePhase(event, event.phase);
	}

	function invokePhase(event:UiEvent, phase:String):Void {
		var values = handlers.get(handlerKey(event.kind, phase));
		if (values == null)
			return;
		for (handler in values) {
			handler(event);
			if (event.propagationStopped)
				return;
		}
	}

	static inline function handlerKey(kind:String, phase:String):String
		return kind + "#" + phase;

	public function find(id:WidgetId):Null<RenderNode> {
		if (id == null)
			return null;
		if (this.id.equals(id))
			return this;
		for (child in children) {
			var found = child.find(id);
			if (found != null)
				return found;
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
