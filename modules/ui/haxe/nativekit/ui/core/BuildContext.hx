package nativekit.ui.core;

import FontCollection;
import NativeKitSurface;
import nativekit.ui.theme.Theme;
import nativekit.ui.gestures.GestureArena;

/** Frame-local identity scopes backed by a persistent UiContext state store. */
class BuildContext {
	public final stateStore:StateStore;
	public var fonts(default, null):Null<FontCollection>;
	public var platformSurface(default, null):Null<NativeKitSurface>;
	public final textInput:TextInputBridge;
	public final clipboard:ClipboardService;
	public final gestures:GestureArena;
	public var theme(default, null):Theme;
	final claimed:Map<Int, String>;
	var scope:KeyScope;

	public function new(stateStore:StateStore, ?fonts:FontCollection, ?textInput:TextInputBridge,
			?clipboard:ClipboardService, ?theme:Theme, ?gestures:GestureArena) {
		if (stateStore == null)
			throw "Build context requires a state store";
		this.stateStore = stateStore;
		this.fonts = fonts;
		platformSurface = null;
		this.textInput = textInput == null ? new TextInputBridge() : textInput;
		this.clipboard = clipboard == null ? new ClipboardService() : clipboard;
		this.gestures = gestures == null ? new GestureArena() : gestures;
		this.theme = theme == null ? new Theme() : theme;
		claimed = new Map();
		scope = new KeyScope();
	}

	/** Replaces the palette used by subsequently built widgets. */
	public function setTheme(theme:Theme):Void {
		if (theme == null)
			throw "Build context requires a theme";
		this.theme = theme;
	}

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

	public function beginFrame():Void {
		claimed.clear();
		scope = new KeyScope();
	}

	public function id(localKey:String):WidgetId {
		var id = scope.widgetId(localKey);
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
		var value:State<Dynamic> = new State<Dynamic>(stateStore, id);
		return cast value;
	}

	/** Opens an already initialized value without supplying an unused placeholder. */
	public function existingState<T>(id:WidgetId):State<T> {
		if (!stateStore.contains(id))
			throw "Widget state has not been initialized";
		var value:State<Dynamic> = new State<Dynamic>(stateStore, id);
		return cast value;
	}
}
