package nativekit.ui.core;

import FontCollection;
import NativeKitSurface;
import LayoutStyle;
import nativekit.ui.theme.Theme;
import nativekit.ui.theme.TextRole;
import nativekit.ui.style.StyleSheet;
import nativekit.ui.style.StyleResolver;
import nativekit.ui.style.StyleEnvironment;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.gestures.GestureArena;
import nativekit.ui.animation.AnimationScheduler;

/** Frame-local identity scopes backed by a persistent UiContext state store. */
class BuildContext {
	public final stateStore:StateStore;
	public var fonts(default, null):Null<FontCollection>;
	public var platformSurface(default, null):Null<NativeKitSurface>;
	public final textInput:TextInputBridge;
	public final clipboard:ClipboardService;
	public final gestures:GestureArena;
	public final animations:AnimationScheduler;
	public final interactionStates:InteractionStateStore;
	public final styleResolver:StyleResolver;
	public final environment:StyleEnvironment;
	public var theme(default, null):Theme;
	public var styleSheet(default, null):StyleSheet;
	/** Logical viewport dimensions for frame-local placement decisions. */
	public var viewportWidth(default, null):Float;
	public var viewportHeight(default, null):Float;
	var styleParent:Null<ComputedStyle>;
	var focusRequester:WidgetId->Bool;
	final claimed:Map<Int, String>;
	var scope:KeyScope;
	var textStyleStack:Array<ResolvedTextStyle>;

	public function new(stateStore:StateStore, ?fonts:FontCollection, ?textInput:TextInputBridge,
			?clipboard:ClipboardService, ?theme:Theme, ?gestures:GestureArena,
			?animations:AnimationScheduler, ?styleSheet:StyleSheet,
			?interactionStates:InteractionStateStore) {
		if (stateStore == null)
			throw "Build context requires a state store";
		this.stateStore = stateStore;
		this.fonts = fonts;
		platformSurface = null;
		this.textInput = textInput == null ? new TextInputBridge() : textInput;
		this.clipboard = clipboard == null ? new ClipboardService() : clipboard;
		this.gestures = gestures == null ? new GestureArena() : gestures;
		this.animations = animations == null ? new AnimationScheduler() : animations;
		this.interactionStates = interactionStates == null ? new InteractionStateStore() : interactionStates;
		this.styleResolver = new StyleResolver(this.animations);
		this.environment = new StyleEnvironment();
		this.theme = theme == null ? new Theme() : theme;
		this.theme.refreshStyles();
		this.styleSheet = styleSheet == null ? new StyleSheet("Application") : styleSheet;
		styleParent = null;
		viewportWidth = 0.0;
		viewportHeight = 0.0;
		focusRequester = function(_) { return false; };
		claimed = new Map();
		scope = new KeyScope();
		textStyleStack = [ResolvedTextStyle.fromTheme(this.theme)];
	}

	/** Replaces the palette used by subsequently built widgets. */
	public function setTheme(theme:Theme):Void {
		if (theme == null)
			throw "Build context requires a theme";
		this.theme = theme;
		this.theme.refreshStyles();
		if (textStyleStack.length <= 1)
			textStyleStack = [ResolvedTextStyle.fromTheme(theme)];
	}

	/** Replaces the application stylesheet layered above the active theme. */
	public function setStyleSheet(styleSheet:StyleSheet):Void {
		if (styleSheet == null)
			throw "Build context requires a stylesheet";
		this.styleSheet = styleSheet;
	}

	/** Updates viewport-derived environment values for conditional style rules. */
	public function setEnvironmentViewport(width:Float, height:Float):Void
		environment.setViewport(width, height);

	/** Computed inherited style supplied by the nearest composing parent. */
	public var inheritedStyle(get, never):Null<ComputedStyle>;
	function get_inheritedStyle():Null<ComputedStyle>
		return styleParent;

	/** Builds descendants with the given computed style as their inheritance source. */
	public function withStyleParent<T>(parent:ComputedStyle, build:Void->T):T {
		if (parent == null || build == null)
			throw "A style parent and build callback are required";
		var previous = styleParent;
		styleParent = parent;
		try {
			var result = build();
			styleParent = previous;
			return result;
		} catch (error:Dynamic) {
			styleParent = previous;
			throw error;
		}
	}

	/** Resolves one node against the active inherited style and current layers. */
	public function resolveStyle(target:StyleTarget, local:Null<LayoutStyle>):ComputedStyle
		return styleResolver.resolve(target, styleParent, theme.styles, styleSheet, local, environment);

	/** Revision fingerprint used to classify style work before the next submission. */
	public var styleRevision(get, never):Int;
	function get_styleRevision():Int
		return theme.styles.revision * 1000003 + styleSheet.revision * 1009 + environment.revision;

	/** Installs the UiContext focus route used by composite keyboard widgets. */
	public function setFocusRequester(requester:WidgetId->Bool):Void {
		if (requester == null)
			throw "Build context requires a focus request route";
		focusRequester = requester;
	}

	public function requestFocus(id:WidgetId):Bool
		return id != null && focusRequester(id);

	/** Provides the font collection used by text-layout-backed widgets. */
	public function setFonts(fonts:FontCollection):Void {
		if (fonts == null || fonts.isDisposed())
			throw "Build context requires a live font collection";
		this.fonts = fonts;
	}

	/** Provides the NativeKit surface used for IME and other platform services. */
	public function setPlatformSurface(surface:NativeKitSurface):Void {
		if (surface == null || surface.isDisposed())
			throw "Build context requires a live NativeKit surface";
		platformSurface = surface;
		textInput.attach(surface);
	}

	/** Installs the logical viewport used while building the current frame. */
	public function setViewport(width:Float, height:Float):Void {
		if (width <= 0.0 || height <= 0.0 || !finite(width) || !finite(height))
			throw "Build viewport dimensions must be positive and finite";
		viewportWidth = width;
		viewportHeight = height;
	}

	public function beginFrame():Void {
		claimed.clear();
		scope = new KeyScope();
		stateStore.beginFrame();
		textStyleStack = [ResolvedTextStyle.fromTheme(theme)];
	}

	/** Returns the concrete typography currently inherited by the build. */
	public function currentTextStyle():ResolvedTextStyle
		return textStyleStack[textStyleStack.length - 1];

	/** Resolves a local sparse override against the current inherited style. */
	public function resolveTextStyle(?override:TextStyleOverride):ResolvedTextStyle
		return currentTextStyle().merge(override);

	/** Resolves a semantic theme role, then applies an optional local override. */
	public function resolveTextRole(role:TextRole,
			?override:TextStyleOverride):ResolvedTextStyle {
		var resolved = role == null || role == TextRole.Body
			? currentTextStyle()
			: currentTextStyle().merge(theme.textRole(role).toOverride());
		return resolved.merge(override);
	}

	/** Builds a subtree under a nested typography scope and restores the parent scope. */
	public function withTextStyle<T>(override:TextStyleOverride, build:Void->T):T {
		if (override == null || build == null)
			throw "A text style scope requires a style and callback";
		textStyleStack.push(resolveTextStyle(override));
		try {
			var result = build();
			textStyleStack.pop();
			return result;
		} catch (error:Dynamic) {
			textStyleStack.pop();
			throw error;
		}
	}

	public function id(localKey:String):WidgetId {
		var id = scope.widgetId(localKey);
		stateStore.rememberPath(id, scope.pathValue() + localKey.length + ":" + localKey);
		if (claimed.exists(id.value))
			throw 'Duplicate widget ID ${id.value}; use distinct keys for sibling views';
		claimed.set(id.value, scope.pathValue() + localKey);
		return id;
	}

	public function withScope<T>(key:Key, build:Void->T):T {
		if (key == null || build == null)
			throw "A scoped build requires a key and callback";
		var previous = scope;
		scope = scope.child(key);
		try {
			var result = build();
			scope = previous;
			return result;
		} catch (error:Dynamic) {
			scope = previous;
			throw error;
		}
	}

	public function state<T>(id:WidgetId, initial:T):State<T> {
		stateStore.initialize(id, initial);
		return new State<T>(stateStore, id);
	}

	/** Opens an already initialized value without supplying an unused placeholder. */
	public function existingState<T>(id:WidgetId):State<T> {
		if (!stateStore.contains(id))
			throw 'Widget state has not been initialized for ${stateStore.describe(id)}';
		stateStore.touch(id);
		return new State<T>(stateStore, id);
	}

	/** Lazily creates resource-backed state and releases it when its widget unmounts. */
	public function resourceState<T>(id:WidgetId, create:Void->T,
			dispose:T->Void):State<T> {
		if (id == null || create == null || dispose == null)
			throw "Resource state requires an ID, factory, and disposer";
		if (!stateStore.contains(id)) {
			var value = create();
			stateStore.initialize(id, value);
			stateStore.onUnmount(id, function() { dispose(value); });
		} else
			stateStore.touch(id);
		var result:State<Dynamic> = new State<Dynamic>(stateStore, id);
		return cast result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
