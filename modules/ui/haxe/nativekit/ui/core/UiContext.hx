package nativekit.ui.core;

import LayoutFrame;
import LayoutSession;
import Canvas;
import DisplayList;
import Renderer;
import Surface;
import FrameInfo;
import ResolvedLayoutItem;
import FontCollection;
import NativeKitSurface;
import NativeKit;
import NativeKit.Capabilities;
import NativeKit.WindowDecorationRegion;
import NativeKit.WindowDecorationRegionKind;
import NativeKit.WindowHandle;
import nativekit.ui.core.CursorShape as UiCursorShape;
import nativekit.ui.semantics.AccessibilityBridge;
import nativekit.ui.semantics.AccessibilityActionData;
import nativekit.ui.semantics.AccessibilityRequest;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.theme.Theme;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.StyleDiff;
import nativekit.ui.style.StyleSheet;
import nativekit.ui.gestures.GestureArena;
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.debug.AccessibilityAudit;
import nativekit.ui.debug.AccessibilityIssue;
import nativekit.ui.debug.UiFrameMetrics;
import nativekit.ui.debug.UiInspector;
import nativekit.ui.debug.UiNodeSnapshot;
import nativekit.ui.debug.UiStyleInvalidationMetrics;

/** Owns the frame-local render tree and the Haxe-side UI subsystems. */
class UiContext {
	final session:LayoutSession;
	public final stateStore:StateStore;
	public final clipboard:ClipboardService;
	public final buildContext:BuildContext;
	public final focus:FocusManager;
	public final events:EventDispatcher;
	public final gestures:GestureArena;
	public final animations:AnimationScheduler;
	public final interactionStates:InteractionStateStore;
	public var root(default, null):Null<RenderNode>;
	/** Called when an active animation needs another host frame. */
	public var onAnimationFrameRequested:Null<Void->Void>;
	var submittedStateRevision:Int;
	var submittedInteractionRevision:Int;
	var submittedStyleRevision:Int;
	var submittedAnimationRevision:Int;
	var submittedGestureRevision:Int;
	var submittedNodeCount:Int;
	var submittedBuildKey:Null<String>;
	var submittedTheme:Null<Theme>;
	var submittedStyleSheet:Null<StyleSheet>;
	var disposed:Bool;
	var customCanvases:Map<Int, Canvas>;
	var customLists:Map<Int, DisplayList>;
	var customGeometries:Map<Int, ResolvedLayoutItem>;
	var customStyles:Map<Int, Null<ComputedStyle>>;
	var customPaintKeys:Map<Int, String>;
	var customListHasCommands:Map<Int, Bool>;
	var accessibilityBridge:Null<AccessibilityBridge>;
	var accessibilitySurface:Null<NativeKitSurface>;
	var decorationWindow:Null<WindowHandle>;
	var decorationCapabilityChecked:Bool;
	var decorationAvailable:Bool;
	var cursorHandler:Null<CursorShape->Void>;
	var currentCursor:CursorShape;
	var lastFrameMetrics:Null<UiFrameMetrics>;
	var frameNumber:Int;
	var diagnosticStage:Int = 0;
	public final textInput:TextInputBridge;

	public function new(?session:LayoutSession, ?fonts:FontCollection, ?theme:Theme) {
		this.session = session == null ? LayoutSession.create() : session;
		stateStore = new StateStore();
		clipboard = new ClipboardService();
		textInput = new TextInputBridge();
		gestures = new GestureArena();
		animations = new AnimationScheduler();
		interactionStates = new InteractionStateStore();
		buildContext = new BuildContext(stateStore, fonts, textInput, clipboard, theme,
			gestures, animations, null, interactionStates);
		if (fonts != null)
			this.session.setFonts(fonts);
		focus = new FocusManager();
		events = new EventDispatcher(focus, interactionStates);
		root = null;
		onAnimationFrameRequested = null;
		submittedStateRevision = -1;
		submittedInteractionRevision = -1;
		submittedStyleRevision = -1;
		submittedAnimationRevision = -1;
		submittedGestureRevision = -1;
		submittedNodeCount = 0;
		submittedBuildKey = null;
		submittedTheme = null;
		submittedStyleSheet = null;
		disposed = false;
		buildContext.setFocusRequester(function(id) { return focusWidget(id); });
		customCanvases = new Map();
		customLists = new Map();
		customGeometries = new Map();
		customStyles = new Map();
		customPaintKeys = new Map();
		customListHasCommands = new Map();
		accessibilityBridge = null;
		accessibilitySurface = null;
		decorationWindow = null;
		decorationCapabilityChecked = false;
		decorationAvailable = false;
		cursorHandler = null;
		currentCursor = CursorShape.Arrow;
		animations.onFrameRequested = function() {
			if (!disposed && onAnimationFrameRequested != null)
				onAnimationFrameRequested();
		};
		lastFrameMetrics = null;
		frameNumber = 0;
	}

	/** Sets fonts for text-aware widgets and the native layout session. */
	public function setFonts(fonts:FontCollection):Void {
		ensureLive();
		if (fonts == null || fonts.isDisposed())
			throw "UI context requires a live font collection";
		session.setFonts(fonts);
		buildContext.setFonts(fonts);
		submittedStyleRevision = -1;
	}

	/** Sets the palette used by subsequent view builds. */
	public function setTheme(theme:Theme):Void {
		ensureLive();
		buildContext.setTheme(theme);
	}

	/** Sets the application rules layered above the active theme stylesheet. */
	public function setStyleSheet(styleSheet:StyleSheet):Void {
		ensureLive();
		buildContext.setStyleSheet(styleSheet);
	}

	/** Attaches the host surface used by platform text-input synchronization. */
	public function attachPlatformSurface(surface:NativeKitSurface):Void {
		ensureLive();
		if (surface == null || surface.isDisposed())
			throw "UI context requires a live NativeKit surface";
		buildContext.setPlatformSurface(surface);
	}

	/** Attaches the NativeKit-owned window that receives declarative chrome regions. */
	public function attachPlatformWindow(window:WindowHandle):Void {
		ensureLive();
		if (window == null || !window.isValid())
			throw "UI context requires a live NativeKit window";
		if (decorationWindow != null)
			clearWindowDecorations();
		decorationWindow = window;
		decorationCapabilityChecked = false;
		decorationAvailable = false;
		if (root != null)
			syncWindowDecorations(root);
	}

	/** Detaches the window and clears any regions previously projected by this context. */
	public function detachPlatformWindow():Void {
		ensureLive();
		if (decorationWindow != null)
			clearWindowDecorations();
		decorationWindow = null;
	}

	/** Builds a fresh view tree, resolves native layout, and reconnects geometry by stable ID. */
	public function submit(view:View, frame:LayoutFrame):RenderNode
		return submitInternal(frame, null, function() return view);

	/**
	 * Submits a lazily built view and reuses the prior tree when the complete
	 * caller-provided build key and all framework revisions are unchanged.
	 */
	public function submitCached(build:Void->View, frame:LayoutFrame,
			cacheKey:String):RenderNode {
		if (build == null)
			throw "Cached UI submissions require a build callback";
		if (cacheKey == null || cacheKey.length == 0)
			throw "Cached UI submissions require a non-empty cache key";
		return submitInternal(frame, cacheKey, build);
	}

	function submitInternal(frame:LayoutFrame, cacheKey:Null<String>, build:Void->View):RenderNode {
		var submitStartedAt = Sys.time();
		diagnosticStage = 1;
		ensureLive();
		if (frame == null || build == null)
			throw "A UI frame requires a view and layout frame";
		var styleResolutionsBefore = buildContext.styleResolver.resolutions;
		var styleCacheHitsBefore = buildContext.styleResolver.cacheHits;
		var styleCacheMissesBefore = buildContext.styleResolver.cacheMisses;
		diagnosticStage = 2;
		gestures.advance(frame.deltaSeconds);
		diagnosticStage = 3;
		animations.advance(frame.deltaSeconds);
		buildContext.setViewport(frame.width, frame.height);
		buildContext.setEnvironmentViewport(frame.width, frame.height);
		if (cacheKey != null && canReuseSubmittedFrame(cacheKey)) {
			frameNumber++;
			lastFrameMetrics = new UiFrameMetrics(frameNumber, submittedNodeCount, 0, 0, 0,
				buildContext.styleResolver.cachedStyleCount, 0, submittedNodeCount, UiDirtyFlag.None,
				0, 0, 0, 0, 0, Sys.time() - submitStartedAt);
			lastFrameMetrics.markReusedSubmission();
			diagnosticStage = 0;
			return cast root;
		}
		buildContext.beginFrame();
		diagnosticStage = 4;
		var view = build();
		var next = buildContext.withScope(new nativekit.ui.core.Key("root"), function() return view == null ? null : view.build(buildContext));
		if (next == null || next.parent != null)
			throw "A view must produce one unparented render tree root";
		diagnosticStage = 5;
		var styleInvalidation = UiStyleInvalidationMetrics.compare(root, next);
		var resolved = session.submit(next.layout, frame);
		diagnosticStage = 6;
		var byId = new Map<Int, ResolvedLayoutItem>();
		for (item in resolved)
			byId.set(item.id, item);
		var resolvedStateRevision = stateStore.revision;
		var missing = false;
		var nodeCount = 0;
		diagnosticStage = 7;
		next.walk(function(node) {
			nodeCount++;
			// Geometry is keyed by the exact LayoutNode ID serialized to NativeUI.
			node.setResolved(byId.get(node.layout.id));
			if (node.resolved == null)
				missing = true;
		});
		if (missing) {
			diagnosticStage = 8;
			throw "Native layout did not return geometry for every render node";
		}
		syncWindowDecorations(next);
		stateStore.endFrame();
		diagnosticStage = 9;
		var previousFocus = focus.focusedId;
		focus.rebuild(next);
		var nextFocus = focus.focusedId;
		if (previousFocus != null && (nextFocus == null || !previousFocus.equals(nextFocus)))
			events.focusEvent(previousFocus, UiEventKind.Blur);
		root = next;
		events.setRoot(next);
		gestures.setRoot(next);
		updateCursor();
		if (nextFocus != null && (previousFocus == null || !previousFocus.equals(nextFocus)))
			events.focusEvent(nextFocus, UiEventKind.Focus);
		submittedStateRevision = resolvedStateRevision;
		submittedInteractionRevision = interactionStates.revision;
		submittedStyleRevision = buildContext.styleRevision;
		submittedAnimationRevision = animations.revision;
		submittedGestureRevision = gestures.revision;
		submittedNodeCount = nodeCount;
		submittedBuildKey = cacheKey;
		submittedTheme = buildContext.theme;
		submittedStyleSheet = buildContext.styleSheet;
		if (accessibilityBridge != null)
			accessibilityBridge.update(next, focus.focusedId);
		frameNumber++;
		lastFrameMetrics = new UiFrameMetrics(frameNumber, nodeCount,
			buildContext.styleResolver.resolutions - styleResolutionsBefore,
			buildContext.styleResolver.cacheHits - styleCacheHitsBefore,
			buildContext.styleResolver.cacheMisses - styleCacheMissesBefore,
			buildContext.styleResolver.cachedStyleCount,
			styleInvalidation.styleChangedNodes, styleInvalidation.styleUnchangedNodes,
			styleInvalidation.invalidationFlags, styleInvalidation.layoutInvalidatedNodes,
			styleInvalidation.textLayoutInvalidatedNodes, styleInvalidation.paintInvalidatedNodes,
			styleInvalidation.compositeInvalidatedNodes, styleInvalidation.semanticsInvalidatedNodes,
			Sys.time() - submitStartedAt);
		diagnosticStage = 0;
		return next;
	}

	public function getDiagnosticStage():Int
		return diagnosticStage;

	/** Connects this frame's semantic projection to a NativeKit platform surface. */
	public function updateAccessibility(surface:NativeKitSurface):Void {
		ensureLive();
		if (surface == null || surface.isDisposed())
			throw "Accessibility projection requires a live NativeKit surface";
		if (accessibilitySurface != surface) {
			if (accessibilityBridge != null)
				accessibilityBridge.dispose();
			accessibilitySurface = surface;
			accessibilityBridge = new AccessibilityBridge(surface);
		}
		accessibilityBridge.update(root, focus.focusedId);
	}

	public function render(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		var renderStartedAt = Sys.time();
		diagnosticStage = 20;
		ensureLive();
		if (root == null)
			throw "Submit a view before rendering the UI context";
		diagnosticStage = 21;
		session.clearCustomPaints();
		var painted = new Map<Int, Bool>();
		var paintedNodes = 0;
		var paintSkippedNodes = 0;
		var emptyPaintNodes = 0;
		diagnosticStage = 22;
		root.walk(function(node) {
			if (!node.hasPaintHandler() || node.resolved == null)
				return;
			if (node.resolved.visible &&
				(node.resolved.width <= 0.0 || node.resolved.height <= 0.0))
				emptyPaintNodes++;
			if (node.resolved.width <= 0.0 || node.resolved.height <= 0.0 ||
				node.resolved.clipBounds.width <= 0.0 || node.resolved.clipBounds.height <= 0.0)
				return;
			var nodeId = node.id.value;
			var displayList = customLists.get(nodeId);
			if (displayList != null && canReuseCustomPaint(node)) {
				if (customListHasCommands.get(nodeId) == true)
					session.setCustomPaint(nodeId, displayList);
				paintSkippedNodes++;
				painted.set(nodeId, true);
				return;
			}
			var canvas = customCanvases.get(nodeId);
			if (canvas == null) {
				canvas = new Canvas();
				customCanvases.set(nodeId, canvas);
			}
			canvas.reset();
			var geometry:ResolvedLayoutItem = cast node.resolved;
			canvas.withState(function(target) {
				target.resetTransform();
				target.clip(geometry.clipBounds);
				target.setTransform(geometry.transform);
				target.translate(geometry.x, geometry.y);
				node.paint(target);
			});
			if (displayList == null) {
				displayList = DisplayList.create();
				customLists.set(nodeId, displayList);
			}
			canvas.update(displayList);
			var hasCommands = displayList.info().commandCount > 0;
			customListHasCommands.set(nodeId, hasCommands);
			if (hasCommands)
				session.setCustomPaint(nodeId, displayList);
			customGeometries.set(nodeId, geometry);
			customStyles.set(nodeId, node.computedStyle);
			customPaintKeys.set(nodeId, node.retainedPaintKey());
			paintedNodes++;
			painted.set(nodeId, true);
		});
		diagnosticStage = 23;
		var stale:Array<Int> = [];
		for (nodeId in customLists.keys())
			if (!painted.exists(nodeId))
				stale.push(nodeId);
		for (nodeId in stale) {
			var canvas = customCanvases.get(nodeId);
			if (canvas != null)
				canvas.reset();
			var displayList = customLists.get(nodeId);
			if (displayList != null)
				displayList.dispose();
			customCanvases.remove(nodeId);
			customLists.remove(nodeId);
			customGeometries.remove(nodeId);
			customStyles.remove(nodeId);
			customPaintKeys.remove(nodeId);
			customListHasCommands.remove(nodeId);
		}
		diagnosticStage = 24;
		session.render(renderer, surface, frame);
		if (lastFrameMetrics != null)
			lastFrameMetrics.completeRender(Sys.time() - renderStartedAt, paintedNodes,
				paintSkippedNodes, emptyPaintNodes);
		diagnosticStage = 0;
	}

	/**
	 * Advances active animations and repaints the retained tree without relayout.
	 * Hosts should use this for animation-only frames instead of submit().
	 */
	public function renderAnimationFrame(renderer:Renderer, surface:Surface,
			frame:FrameInfo, deltaSeconds:Float):Void {
		ensureLive();
		animations.advance(deltaSeconds);
		render(renderer, surface, frame);
	}

	public function focusWidget(id:WidgetId):Bool {
		ensureLive();
		var previous = focus.focusedId;
		if (!focus.focus(id))
			return false;
		dispatchFocusChange(previous, focus.focusedId);
		return true;
	}

	public function focusNext():Null<WidgetId> {
		ensureLive();
		var previous = focus.focusedId;
		var next = focus.focusNext();
		dispatchFocusChange(previous, next);
		return next;
	}

	public function focusPrevious():Null<WidgetId> {
		ensureLive();
		var previous = focus.focusedId;
		var next = focus.focusPrevious();
		dispatchFocusChange(previous, next);
		return next;
	}

	public function clearFocus():Void {
		ensureLive();
		var previous = focus.focusedId;
		focus.focus(null);
		dispatchFocusChange(previous, null);
	}

	/** Routes a NativeKit platform accessibility action to the addressed semantic node. */
	public function accessibilityAction(id:Int, action:Int, value:Null<String>,
			selectionStart:Int, selectionEnd:Int, granularity:Int):Bool {
		ensureLive();
		if (root == null || id <= 0)
			return false;
		var widgetId = new WidgetId(id);
		var node = root.find(widgetId);
		if (node == null || node.semantics == null || node.resolved == null || !node.resolved.visible)
			return false;
		var semantics:Semantics = cast node.semantics;
		var request = AccessibilityRequest.create(action, value, selectionStart,
			selectionEnd, granularity);
		if (request == null)
			return false;
		var enabled = enabledAlongPath(node);
		var actions = semantics.actions;
		if (node.focusable && enabled)
			actions |= nativekit.ui.semantics.AccessibilityAction.Focus;
		if (!enabled)
			actions = 0;
		if ((actions & request.capability) == 0)
			return false;
		var geometry:ResolvedLayoutItem = cast node.resolved;
		if (action == AccessibilityRequest.Focus)
			return focusWidget(widgetId);
		if (action == AccessibilityRequest.ClearFocus) {
			if (focus.focusedId != null && focus.focusedId.equals(widgetId))
				clearFocus();
			return true;
		}
		var deltaX = 0.0;
		var deltaY = 0.0;
		if (action == AccessibilityRequest.ScrollForward)
			deltaY = geometry.height;
		else if (action == AccessibilityRequest.ScrollBackward)
			deltaY = -geometry.height;
		if (action == AccessibilityRequest.ScrollForward ||
			action == AccessibilityRequest.ScrollBackward)
			return events.targetEvent(UiEventKind.Scroll, widgetId, null, deltaX,
				deltaY, new AccessibilityActionData(action, selectionStart, selectionEnd, granularity));
		return events.targetEvent(request.kind, widgetId, request.value, 0.0, 0.0,
			new AccessibilityActionData(action, selectionStart, selectionEnd, granularity));
	}

	/** Clears transient pointer state and reports loss of platform window focus. */
	public function windowFocusLost():Void {
		ensureLive();
		events.cancelPointers();
		updateCursor();
		gestures.cancelAll();
		var previous = focus.focusedId;
		if (previous != null)
			events.focusEvent(previous, UiEventKind.FocusLost);
		focus.focus(null);
		dispatchFocusChange(previous, null);
	}

	public function pointerMove(x:Float, y:Float, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerMove(x, y, modifiers, pointerId, data);
		updateCursor();
	}

	public function pointerDown(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null, timestamp:Float = -1.0):Void {
		ensureLive();
		events.pointerDown(x, y, button, modifiers, pointerId, data, timestamp);
		updateCursor();
	}

	public function pointerUp(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerUp(x, y, button, modifiers, pointerId, data);
		updateCursor();
	}

	public function pointerCancel(pointerId:Int, x:Float, y:Float,
			modifiers:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerCancel(pointerId, x, y, modifiers, data);
		updateCursor();
	}

	public function pointerLeave(pointerId:Int = 0):Void {
		ensureLive();
		events.clearPointer(pointerId);
		updateCursor();
	}

	/** Installs the platform bridge that applies the context's effective cursor. */
	public function setCursorHandler(handler:Null<CursorShape->Void>):Void {
		ensureLive();
		cursorHandler = handler;
		if (handler != null)
			handler(currentCursor);
	}

	/** Installs the host bridge used for physical window/surface pointer capture. */
	public function setPointerCaptureHandler(handler:Null<Bool->Void>):Void {
		ensureLive();
		events.setPointerCaptureHandler(handler);
	}

	public function scroll(x:Float, y:Float, deltaX:Float, deltaY:Float, modifiers:Int = 0):Void {
		ensureLive();
		events.scroll(x, y, deltaX, deltaY, modifiers);
	}

	public function key(kind:String, key:Int, modifiers:Int = 0, scancode:Int = 0):Void {
		ensureLive();
		events.key(kind, key, modifiers, scancode);
	}

	public function text(kind:String, value:Null<String>, data:Dynamic = null):Void {
		ensureLive();
		events.text(kind, value, data);
	}

	function canReuseSubmittedFrame(cacheKey:String):Bool {
		return root != null && submittedBuildKey == cacheKey && dirtyFlags == UiDirtyFlag.None &&
			animations.revision == submittedAnimationRevision && gestures.revision == submittedGestureRevision &&
			buildContext.theme == submittedTheme &&
			buildContext.styleSheet == submittedStyleSheet;
	}

	function canReuseCustomPaint(node:RenderNode):Bool {
		var nodeId = node.id.value;
		var key = node.retainedPaintKey();
		if (key == null || !customPaintKeys.exists(nodeId) ||
			!customListHasCommands.exists(nodeId) || customPaintKeys.get(nodeId) != key)
			return false;
		var previousGeometry = customGeometries.get(nodeId);
		if (previousGeometry == null || node.resolved == null ||
			!sameGeometry(previousGeometry, node.resolved))
			return false;
		return !StyleDiff.compare(customStyles.get(nodeId), node.computedStyle).changed;
	}

	static function sameGeometry(left:ResolvedLayoutItem, right:ResolvedLayoutItem):Bool {
		return left.flags == right.flags && left.x == right.x && left.y == right.y &&
			left.width == right.width && left.height == right.height && left.baseline == right.baseline &&
			sameRect(left.clipBounds, right.clipBounds) && sameRect(left.contentBounds, right.contentBounds) &&
			left.transform.a == right.transform.a && left.transform.b == right.transform.b &&
			left.transform.c == right.transform.c && left.transform.d == right.transform.d &&
			left.transform.tx == right.transform.tx && left.transform.ty == right.transform.ty;
	}

	static inline function sameRect(left:Rect, right:Rect):Bool
		return left.x == right.x && left.y == right.y && left.width == right.width &&
			left.height == right.height;

	public function isDirty():Bool
		return dirtyFlags != UiDirtyFlag.None;

	public var needsAnimationFrame(get, never):Bool;
	inline function get_needsAnimationFrame():Bool
		return animations.activeCount > 0;

	/** Reports pending work categories without changing the current full-submit behavior. */
	public var dirtyFlags(get, never):Int;
	function get_dirtyFlags():Int {
		var result = 0;
		if (stateStore.revision != submittedStateRevision)
			result |= UiDirtyFlag.NeedsBuild | UiDirtyFlag.NeedsStyle | UiDirtyFlag.NeedsTextLayout |
				UiDirtyFlag.NeedsLayout | UiDirtyFlag.NeedsPaint | UiDirtyFlag.NeedsComposite |
				UiDirtyFlag.NeedsSemantics;
		if (interactionStates.revision != submittedInteractionRevision)
			result |= UiDirtyFlag.NeedsStyle | UiDirtyFlag.NeedsPaint | UiDirtyFlag.NeedsSemantics;
		if (buildContext.styleRevision != submittedStyleRevision)
			result |= UiDirtyFlag.NeedsStyle | UiDirtyFlag.NeedsTextLayout | UiDirtyFlag.NeedsLayout | UiDirtyFlag.NeedsPaint |
				UiDirtyFlag.NeedsComposite | UiDirtyFlag.NeedsSemantics;
		if (animations.activeCount > 0)
			result |= UiDirtyFlag.NeedsComposite;
		return result;
	}

	/** Returns a deterministic headless snapshot of the most recently submitted tree. */
	public function inspect():Array<UiNodeSnapshot> {
		ensureLive();
		return UiInspector.snapshot(root, focus.focusedId, events.hoveredId(), events.pressedId());
	}

	/** Timing and style-cache counters captured during the latest submit/render. */
	public var frameMetrics(get, never):Null<UiFrameMetrics>;
	function get_frameMetrics():Null<UiFrameMetrics>
		return lastFrameMetrics;

	/** Formats the current render and semantic tree for logs or developer tools. */
	public function dumpTree():String {
		ensureLive();
		return UiInspector.dump(root, focus.focusedId);
	}

	/** Audits common accessibility naming and focus-geometry mistakes. */
	public function auditAccessibility():Array<AccessibilityIssue> {
		ensureLive();
		return AccessibilityAudit.inspect(root);
	}

	public function dispose():Void {
		if (disposed)
			return;
		session.clearCustomPaints();
		for (nodeId in customCanvases.keys()) {
			var canvas = customCanvases.get(nodeId);
			if (canvas != null)
				canvas.reset();
		}
		for (nodeId in customLists.keys()) {
			var displayList = customLists.get(nodeId);
			if (displayList != null)
				displayList.dispose();
		}
		customCanvases = new Map();
		customLists = new Map();
		customGeometries = new Map();
		customStyles = new Map();
		customPaintKeys = new Map();
		customListHasCommands = new Map();
		session.dispose();
		clipboard.dispose();
		textInput.dispose();
		gestures.cancelAll();
		if (cursorHandler != null)
			cursorHandler(CursorShape.Arrow);
		cursorHandler = null;
		events.setPointerCaptureHandler(null);
		currentCursor = CursorShape.Arrow;
		animations.cancelAll();
		if (accessibilityBridge != null)
			accessibilityBridge.dispose();
		if (decorationWindow != null)
			clearWindowDecorations();
		decorationWindow = null;
		stateStore.dispose();
		interactionStates.dispose();
		disposed = true;
		root = null;
		accessibilityBridge = null;
		accessibilitySurface = null;
	}

	function updateCursor():Void {
		var next = events.cursorShape();
		if (next == currentCursor)
			return;
		currentCursor = next;
		if (cursorHandler != null)
			cursorHandler(next);
	}

	function syncWindowDecorations(tree:RenderNode):Void {
		if (decorationWindow == null || !supportsWindowDecorations())
			return;
		var regions:Array<WindowDecorationRegion> = [];
		tree.walk(function(node) {
			if (node.windowDecoration == null || node.resolved == null ||
				!node.resolved.visible)
				return;
			var bounds = visibleWindowBounds(node.resolved);
			var left = Math.max(0.0, bounds.x);
			var top = Math.max(0.0, bounds.y);
			var right = Math.min(buildContext.viewportWidth, bounds.x + bounds.width);
			var bottom = Math.min(buildContext.viewportHeight, bounds.y + bounds.height);
			if (right <= left || bottom <= top)
				return;
			var region = new WindowDecorationRegion();
			region.set_x(left);
			region.set_y(top);
			region.set_width(right - left);
			region.set_height(bottom - top);
			var kind:WindowDecorationRegionKind = cast node.windowDecoration;
			region.set_kind(kind);
			region.set_cursor_shape(node.windowDecorationCursor == null
				? 0 : nativeCursorShape(node.windowDecorationCursor));
			regions.push(region);
		});
		var window:WindowHandle = cast decorationWindow;
		NativeKit.nk_window_set_decoration_regions_checked(window, regions);
	}

	function clearWindowDecorations():Void {
		if (decorationWindow == null || !supportsWindowDecorations())
			return;
		try {
			var window:WindowHandle = cast decorationWindow;
			NativeKit.nk_window_set_decoration_regions_checked(window, []);
		} catch (_:Dynamic) {
			// Context disposal must remain best-effort if the host already destroyed the window.
		}
	}

	function supportsWindowDecorations():Bool {
		if (decorationCapabilityChecked)
			return decorationAvailable;
		decorationCapabilityChecked = true;
		var capabilities = NativeKit.nk_get_capabilities();
		decorationAvailable = haxe.Int64.compare(
			haxe.Int64.and(capabilities, Capabilities.windowCustomDecorations()),
			haxe.Int64.ofInt(0)) != 0;
		return decorationAvailable;
	}

	static function visibleWindowBounds(item:ResolvedLayoutItem):Rect {
		var transform = item.transform;
		var x0 = transform.a * item.x + transform.c * item.y + transform.tx;
		var y0 = transform.b * item.x + transform.d * item.y + transform.ty;
		var x1 = transform.a * (item.x + item.width) + transform.c * item.y + transform.tx;
		var y1 = transform.b * (item.x + item.width) + transform.d * item.y + transform.ty;
		var x2 = transform.a * item.x + transform.c * (item.y + item.height) + transform.tx;
		var y2 = transform.b * item.x + transform.d * (item.y + item.height) + transform.ty;
		var x3 = transform.a * (item.x + item.width) + transform.c * (item.y + item.height) + transform.tx;
		var y3 = transform.b * (item.x + item.width) + transform.d * (item.y + item.height) + transform.ty;
		var left = Math.max(item.clipBounds.x, Math.min(Math.min(x0, x1), Math.min(x2, x3)));
		var top = Math.max(item.clipBounds.y, Math.min(Math.min(y0, y1), Math.min(y2, y3)));
		var right = Math.min(item.clipBounds.x + item.clipBounds.width,
			Math.max(Math.max(x0, x1), Math.max(x2, x3)));
		var bottom = Math.min(item.clipBounds.y + item.clipBounds.height,
			Math.max(Math.max(y0, y1), Math.max(y2, y3)));
		return new Rect(left, top, Math.max(0.0, right - left), Math.max(0.0, bottom - top));
	}

	static function nativeCursorShape(shape:UiCursorShape):Int {
		return cast switch shape {
			case UiCursorShape.Arrow: NativeKit.CursorShape.Arrow;
			case UiCursorShape.Text: NativeKit.CursorShape.Ibeam;
			case UiCursorShape.Crosshair: NativeKit.CursorShape.Crosshair;
			case UiCursorShape.Hand: NativeKit.CursorShape.Hand;
			case UiCursorShape.HorizontalResize: NativeKit.CursorShape.HorizontalResize;
			case UiCursorShape.VerticalResize: NativeKit.CursorShape.VerticalResize;
			case UiCursorShape.DiagonalResize: NativeKit.CursorShape.NwseResize;
			case UiCursorShape.DiagonalResizeNesw: NativeKit.CursorShape.NeswResize;
			case UiCursorShape.Move: NativeKit.CursorShape.Move;
			case UiCursorShape.NotAllowed: NativeKit.CursorShape.NotAllowed;
			case _: NativeKit.CursorShape.Arrow;
		};
	}

	function dispatchFocusChange(previous:Null<WidgetId>, next:Null<WidgetId>):Void {
		if (previous != null && (next == null || !previous.equals(next)))
			events.focusEvent(previous, UiEventKind.Blur);
		if (next != null && (previous == null || !previous.equals(next)))
			events.focusEvent(next, UiEventKind.Focus);
	}

	function ensureLive():Void {
		if (disposed)
			throw "UI context has been disposed";
	}

	static function enabledAlongPath(node:RenderNode):Bool {
		var current:Null<RenderNode> = node;
		while (current != null) {
			var present:RenderNode = cast current;
			if (!present.enabled)
				return false;
			current = present.parent;
		}
		return true;
	}
}
