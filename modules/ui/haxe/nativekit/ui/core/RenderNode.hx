package nativekit.ui.core;

import LayoutNode;
import LayoutStyle;
import LayoutVisualKind;
import Canvas;
import CompositeMode;
import ResolvedLayoutItem;
import Point;
import Rect;
import NativeKit.WindowDecorationRegionKind;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.Decoration;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.style.StyleDiff;
import nativekit.ui.style.StyleImpact;
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
	public var hitTestBehavior:HitTestBehavior;
	/** Controls GPU raster reuse for this node's custom paint. */
	public var cachePolicy:CachePolicy;
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
	/** Native hit-test behavior assigned to this node's resolved bounds. */
	public var windowDecoration:Null<WindowDecorationRegionKind>;
	/** Optional native cursor override for this node's window-chrome region. */
	public var windowDecorationCursor:Null<CursorShape>;
	public var semantics:Null<Semantics>;
	/** Revision of paint/text content consumed by retained scene caches. */
	public var contentRevision(default, null):Int;
	/** Revision of resolved bounds, transforms, clips, and paint order. */
	public var geometryRevision(default, null):Int;
	/** Revision of opacity/effects and other compositing inputs. */
	public var compositeRevision(default, null):Int;
	/** Categories raised while this node was compared with its prior frame. */
	public var invalidationFlags(default, null):Int;
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
		hitTestBehavior = HitTestBehavior.Auto;
		cachePolicy = CachePolicy.None;
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
		windowDecoration = null;
		windowDecorationCursor = null;
		semantics = null;
		contentRevision = 1;
		geometryRevision = 1;
		compositeRevision = 1;
		invalidationFlags = UiDirtyFlag.NeedsBuild | UiDirtyFlag.NeedsStyle |
			UiDirtyFlag.NeedsTextLayout | UiDirtyFlag.NeedsLayout | UiDirtyFlag.NeedsPaint |
			UiDirtyFlag.NeedsComposite | UiDirtyFlag.NeedsSemantics | UiDirtyFlag.NeedsHitGeometry;
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

	/** Converts a point in this node's local space into viewport/global space. */
	public function localToGlobal(point:Point):Point
		return requireResolved().localToViewport(point);

	/** Converts a viewport/global point into this node's local space. */
	public function globalToLocal(point:Point):Point
		return requireResolved().viewportToLocal(point);

	/** Converts a point from this node's local space into another node's local space. */
	public function localToNode(point:Point, other:RenderNode):Point {
		if (other == null)
			throw "Coordinate conversion requires another render node";
		return other.globalToLocal(localToGlobal(point));
	}

	public function localBounds():Rect
		return requireResolved().localBounds();

	public function globalBounds():Rect
		return requireResolved().viewportBounds();

	public function containsGlobalPoint(point:Point):Bool {
		if (point == null)
			throw "Global points cannot be null";
		return requireResolved().hitTest(point.x, point.y);
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

	/** Assigns this node's bounds to the native window-chrome hit-test map. */
	public function setWindowDecoration(kind:WindowDecorationRegionKind,
			?decorationCursor:CursorShape):RenderNode {
		if (kind == null)
			throw "Window decoration regions require a kind";
		windowDecoration = kind;
		windowDecorationCursor = decorationCursor;
		if (decorationCursor != null)
			cursor = decorationCursor;
		if (cursor == null)
			cursor = cursorForWindowDecoration(kind);
		return this;
	}

	static function cursorForWindowDecoration(kind:WindowDecorationRegionKind):CursorShape {
		return switch kind {
			// A drag region behaves like a native title bar: the platform owns
			// the move cursor during the active drag, not while hovering.
			case WindowDecorationRegionKind.Drag: CursorShape.Arrow;
			case WindowDecorationRegionKind.ResizeNorth | WindowDecorationRegionKind.ResizeSouth:
				CursorShape.VerticalResize;
			case WindowDecorationRegionKind.ResizeWest | WindowDecorationRegionKind.ResizeEast:
				CursorShape.HorizontalResize;
			case WindowDecorationRegionKind.ResizeNorthwest | WindowDecorationRegionKind.ResizeSoutheast:
				CursorShape.DiagonalResize;
			case WindowDecorationRegionKind.ResizeNortheast | WindowDecorationRegionKind.ResizeSouthwest:
				CursorShape.DiagonalResizeNesw;
			case WindowDecorationRegionKind.Client: CursorShape.Arrow;
			case _: CursorShape.Arrow;
		};
	}

	/**
	 * Adds a custom paint callback. The callback draws in node-local coordinates
	 * with (0, 0) at the node's top-left corner; native layout owns placement,
	 * transforms, and ancestor clipping. A cache key opts the callback into
	 * retained display-list reuse and must change whenever its local output can
	 * change for reasons other than size or computed style.
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
		return paintHandlers.length > 0 || decorations.length > 0 || hasStyleDecorations();

	/** Copies the framework hit policy into the native transaction payload. */
	@:allow(nativekit.ui.core.UiContext)
	function syncHitTestPolicy():Void {
		switch hitTestBehavior {
			case HitTestBehavior.None:
				layout.hitSelf = false;
				layout.hitChildren = false;
			case HitTestBehavior.SelfOnly:
				layout.hitSelf = hitTestSelf;
				layout.hitChildren = false;
			case HitTestBehavior.ChildrenOnly:
				layout.hitSelf = false;
				layout.hitChildren = true;
			case HitTestBehavior.Auto:
				layout.hitSelf = hitTestSelf;
				layout.hitChildren = true;
		}
	}

	/** Copies retained scene revisions into the native layout transaction payload. */
	@:allow(nativekit.ui.core.UiContext)
	function syncSceneRevisions():Void {
		layout.contentRevision = contentRevision;
		layout.geometryRevision = geometryRevision;
		layout.compositeRevision = compositeRevision;
	}

	/** Returns the complete opt-in fingerprint for safe retained paint reuse. */
	@:allow(nativekit.ui.core.UiContext)
	function retainedPaintKey():Null<String> {
		if (!hasPaintHandler())
			return null;
		var result = "paint";
		var style = computedStyle == null ? null : computedStyle.get(StyleProperty.Decorations);
		if (style != null && style.decorations.length > 0)
			result += "|style-decorations:" + style.key();
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

	/** Fingerprint for local compositor metadata; transforms are native placement. */
	@:allow(nativekit.ui.core.UiContext)
	function retainedCompositeKey():Null<String> {
		if (!hasCompositePaint())
			return null;
		var style = computedStyle == null ? new ComputedStyle() : computedStyle;
		var effects = style.get(StyleProperty.Effects);
		var backdropEffects = style.get(StyleProperty.BackdropEffects);
		var mask = style.get(StyleProperty.Mask);
		var maskKey = mask == null ? "" : mask.describe();
		if (mask != null && mask.image != null)
			maskKey += ":image=" + mask.image.identity;
		return "opacity:" + Std.string(style.get(StyleProperty.Opacity)) +
			"|effects:" + (effects == null ? "" : effects.key()) +
			"|backdrop:" + (backdropEffects == null ? "" : backdropEffects.key()) +
			"|mask:" + maskKey;
	}

	@:allow(nativekit.ui.core.UiContext)
	function setResolved(item:Null<ResolvedLayoutItem>):Void {
		resolved = item;
		if (item != null)
			for (handler in resolvedHandlers)
				handler(item);
	}

	@:allow(nativekit.ui.core.UiContext)
	function paintContent(canvas:Canvas):Bool {
		if (resolved == null || !resolved.visible || !hasPaintHandler())
			return false;
		var style = computedStyle == null ? new ComputedStyle() : computedStyle;
		var styleDecorations = style.get(StyleProperty.Decorations);
		if (styleDecorations != null)
			for (decoration in styleDecorations.decorations)
				decoration.paint(canvas, resolved, style);
		for (decoration in decorations)
			decoration.paint(canvas, resolved, style);
		for (handler in paintHandlers)
			handler(canvas, resolved);
		return true;
	}

	@:allow(nativekit.ui.core.UiContext)
	function hasCompositePaint():Bool {
		if (resolved == null || !resolved.visible || !hasPaintHandler())
			return false;
		var style = computedStyle == null ? new ComputedStyle() : computedStyle;
		var opacity = style.get(StyleProperty.Opacity);
		var effects = style.get(StyleProperty.Effects);
		var hasEffects = effects != null && effects.effects.length > 0;
		var backdropEffects = style.get(StyleProperty.BackdropEffects);
		var hasBackdropEffects = backdropEffects != null && backdropEffects.effects.length > 0;
		var mask = style.get(StyleProperty.Mask);
		return opacity < 1.0 || hasEffects || hasBackdropEffects || mask != null;
	}

	/** Emits only the framework-owned layer metadata; it has no draw commands. */
	@:allow(nativekit.ui.core.UiContext)
	function paintComposite(canvas:Canvas):Bool {
		if (!hasCompositePaint())
			return false;
		var style = computedStyle == null ? new ComputedStyle() : computedStyle;
		var opacity = style.get(StyleProperty.Opacity);
		var effects = style.get(StyleProperty.Effects);
		var hasEffects = effects != null && effects.effects.length > 0;
		var backdropEffects = style.get(StyleProperty.BackdropEffects);
		var hasBackdropEffects = backdropEffects != null && backdropEffects.effects.length > 0;
		var mask = style.get(StyleProperty.Mask);
		canvas.beginLayer(opacity, CompositeMode.SourceOver, resolved.localBounds(),
			hasEffects ? effects : null, mask, hasBackdropEffects ? backdropEffects : null);
		canvas.endLayer();
		return true;
	}

	@:allow(nativekit.ui.core.UiContext)
	function paint(canvas:Canvas):Bool {
		if (!hasPaintHandler() || resolved == null || !resolved.visible)
			return false;
		if (hasCompositePaint()) {
			var content:Canvas->Void = function(target:Canvas) {
				paintContent(target);
			};
			var style = computedStyle == null ? new ComputedStyle() : computedStyle;
			var effects = style.get(StyleProperty.Effects);
			var backdropEffects = style.get(StyleProperty.BackdropEffects);
			var mask = style.get(StyleProperty.Mask);
			canvas.withLayer(style.get(StyleProperty.Opacity), content, CompositeMode.SourceOver,
				resolved.bounds(), effects != null && effects.effects.length > 0 ? effects : null,
				mask, backdropEffects != null && backdropEffects.effects.length > 0 ? backdropEffects : null);
		} else {
			paintContent(canvas);
		}
		return true;
	}

	function hasStyleDecorations():Bool {
		if (computedStyle == null)
			return false;
		var value = computedStyle.get(StyleProperty.Decorations);
		return value != null && value.decorations.length > 0;
	}

	/**
	 * Carries retained-scene revisions across freshly built trees. The view
	 * layer still submits a complete layout transaction, but downstream work
	 * can now distinguish content, geometry, and composition changes.
	 */
	@:allow(nativekit.ui.debug.UiStyleInvalidationMetrics)
	function syncRevisions(previous:Null<RenderNode>, diff:StyleDiff):Void {
		if (previous == null) {
			invalidationFlags = UiDirtyFlag.NeedsBuild | UiDirtyFlag.NeedsStyle |
				UiDirtyFlag.NeedsTextLayout | UiDirtyFlag.NeedsLayout | UiDirtyFlag.NeedsPaint |
				UiDirtyFlag.NeedsComposite | UiDirtyFlag.NeedsSemantics | UiDirtyFlag.NeedsHitGeometry;
			return;
		}

		contentRevision = previous.contentRevision;
		geometryRevision = previous.geometryRevision;
		compositeRevision = previous.compositeRevision;
		invalidationFlags = UiDirtyFlag.None;
		var contentChanged = false;
		var geometryChanged = false;
		var compositeChanged = false;
		if (diff.changed) {
			invalidationFlags = UiDirtyFlag.NeedsStyle | UiDirtyFlag.fromStyleImpact(diff.impact);
			if ((diff.impact & (StyleImpact.Paint | StyleImpact.TextLayout)) != 0)
				contentChanged = true;
			if ((diff.impact & (StyleImpact.Layout | StyleImpact.TextLayout |
				StyleImpact.HitGeometry)) != 0)
				geometryChanged = true;
			if ((diff.impact & StyleImpact.Composite) != 0)
				compositeChanged = true;
		}

		var rawFlags = layoutInputDiffFlags(previous);
		invalidationFlags |= rawFlags;
		if ((rawFlags & (UiDirtyFlag.NeedsPaint | UiDirtyFlag.NeedsTextLayout)) != 0)
			contentChanged = true;
		if ((rawFlags & (UiDirtyFlag.NeedsLayout | UiDirtyFlag.NeedsTextLayout |
			UiDirtyFlag.NeedsHitGeometry)) != 0)
			geometryChanged = true;
		if ((rawFlags & UiDirtyFlag.NeedsComposite) != 0)
			compositeChanged = true;

		// Text and external intrinsic content are not represented by computed
		// style, so classify them explicitly as both content and geometry.
		if (layout.visualKind != previous.layout.visualKind || layout.text != previous.layout.text ||
			layout.measureVersion != previous.layout.measureVersion ||
			layout.intrinsicContent != previous.layout.intrinsicContent) {
			contentChanged = true;
			geometryChanged = true;
			invalidationFlags |= UiDirtyFlag.NeedsTextLayout | UiDirtyFlag.NeedsLayout |
				UiDirtyFlag.NeedsPaint;
		}

		if (hitTestSelf != previous.hitTestSelf || hitTestBehavior != previous.hitTestBehavior) {
			geometryChanged = true;
			invalidationFlags |= UiDirtyFlag.NeedsHitGeometry;
		}
		if (!sameSemantics(semantics, previous.semantics) || enabled != previous.enabled ||
			focusable != previous.focusable || focusTrap != previous.focusTrap ||
			tabIndex != previous.tabIndex)
			invalidationFlags |= UiDirtyFlag.NeedsSemantics;
		if (contentChanged)
			contentRevision++;
		if (geometryChanged)
			geometryRevision++;
		if (compositeChanged)
			compositeRevision++;
	}

	/** Classifies concrete LayoutNode mutations that bypass computed styles. */
	function layoutInputDiffFlags(previous:RenderNode):Int {
		var before = previous.layout;
		var after = layout;
		var flags = 0;
		var beforeStyle = before.style;
		var afterStyle = layout.style;
		if (!sameAxis(beforeStyle.width, afterStyle.width) ||
			!sameAxis(beforeStyle.height, afterStyle.height) ||
			beforeStyle.aspectRatio != afterStyle.aspectRatio ||
			beforeStyle.direction != afterStyle.direction ||
			beforeStyle.childAlignX != afterStyle.childAlignX ||
			beforeStyle.childAlignY != afterStyle.childAlignY ||
			beforeStyle.childDistribution != afterStyle.childDistribution ||
			beforeStyle.positioning != afterStyle.positioning ||
			beforeStyle.wrapMode != afterStyle.wrapMode ||
			beforeStyle.rowGap != afterStyle.rowGap || beforeStyle.columnGap != afterStyle.columnGap ||
			beforeStyle.alignSelf != afterStyle.alignSelf ||
			beforeStyle.positionX != afterStyle.positionX || beforeStyle.positionY != afterStyle.positionY ||
			!sameInsets(beforeStyle.padding, afterStyle.padding) ||
			beforeStyle.childGap != afterStyle.childGap)
			flags |= UiDirtyFlag.NeedsLayout;
		if (beforeStyle.zIndex != afterStyle.zIndex ||
			beforeStyle.clipToParent != afterStyle.clipToParent ||
			!sameColor(beforeStyle.background, afterStyle.background) ||
			beforeStyle.radiusTopLeft != afterStyle.radiusTopLeft ||
			beforeStyle.radiusTopRight != afterStyle.radiusTopRight ||
			beforeStyle.radiusBottomRight != afterStyle.radiusBottomRight ||
			beforeStyle.radiusBottomLeft != afterStyle.radiusBottomLeft ||
			beforeStyle.clipHorizontal != afterStyle.clipHorizontal ||
			beforeStyle.clipVertical != afterStyle.clipVertical ||
			beforeStyle.visible != afterStyle.visible ||
			!sameColor(before.textColor, after.textColor))
			flags |= UiDirtyFlag.NeedsPaint;
		if (beforeStyle.zIndex != afterStyle.zIndex ||
			beforeStyle.clipToParent != afterStyle.clipToParent ||
			beforeStyle.clipHorizontal != afterStyle.clipHorizontal ||
			beforeStyle.clipVertical != afterStyle.clipVertical ||
			beforeStyle.visible != afterStyle.visible)
			flags |= UiDirtyFlag.NeedsHitGeometry;
		if (!sameTransform(beforeStyle.transform, afterStyle.transform) ||
			beforeStyle.transformOriginX != afterStyle.transformOriginX ||
			beforeStyle.transformOriginY != afterStyle.transformOriginY) {
			flags |= UiDirtyFlag.NeedsComposite | UiDirtyFlag.NeedsHitGeometry;
		}
		if (!sameTextStyle(before.textStyle, after.textStyle) ||
			!sameParagraphStyle(before.paragraphStyle, after.paragraphStyle))
			flags |= UiDirtyFlag.NeedsTextLayout | UiDirtyFlag.NeedsPaint | UiDirtyFlag.NeedsLayout;
		return flags;
	}

	static function sameAxis(left:LayoutAxis, right:LayoutAxis):Bool
		return left == right || (left != null && right != null && left.sizing == right.sizing &&
			left.value == right.value && left.min == right.min && left.max == right.max &&
			left.growWeight == right.growWeight);

	static function sameInsets(left:Insets, right:Insets):Bool
		return left == right || (left != null && right != null && left.left == right.left &&
			left.top == right.top && left.right == right.right && left.bottom == right.bottom);

	static function sameColor(left:Color, right:Color):Bool
		return left == right || (left != null && right != null && left.red == right.red &&
			left.green == right.green && left.blue == right.blue && left.alpha == right.alpha);

	static function sameTransform(left:Transform2D, right:Transform2D):Bool
		return left == right || (left != null && right != null && left.a == right.a &&
			left.b == right.b && left.c == right.c && left.d == right.d &&
			left.tx == right.tx && left.ty == right.ty);

	static function sameTextStyle(left:TextStyle, right:TextStyle):Bool
		return left == right || (left != null && right != null && left.font == right.font &&
			left.fontSize == right.fontSize && left.letterSpacing == right.letterSpacing);

	static function sameParagraphStyle(left:ParagraphStyle, right:ParagraphStyle):Bool
		return left == right || (left != null && right != null && left.wrap == right.wrap &&
			left.alignment == right.alignment && left.lineHeight == right.lineHeight &&
			left.direction == right.direction);

	static function sameSemantics(left:Null<Semantics>, right:Null<Semantics>):Bool {
		if (left == right)
			return true;
		if (left == null || right == null)
			return false;
		return left.role == right.role && left.label == right.label && left.value == right.value &&
			left.states == right.states && left.actions == right.actions &&
			left.numericValue == right.numericValue && left.numericMinimum == right.numericMinimum &&
			left.numericMaximum == right.numericMaximum && left.textStart == right.textStart &&
			left.documentLength == right.documentLength && left.selectionStart == right.selectionStart &&
			left.selectionEnd == right.selectionEnd && left.setSize == right.setSize &&
			left.positionInSet == right.positionInSet && left.rowCount == right.rowCount &&
			left.columnCount == right.columnCount && left.rowIndex == right.rowIndex &&
			left.columnIndex == right.columnIndex && left.rowSpan == right.rowSpan &&
			left.columnSpan == right.columnSpan && left.hierarchyLevel == right.hierarchyLevel &&
			left.orientation == right.orientation;
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

	function requireResolved():ResolvedLayoutItem {
		if (resolved == null)
			throw "Render node has no resolved geometry; submit the UI first";
		return cast resolved;
	}
}
