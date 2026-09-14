import Color;
import Canvas;
import FontCollection;
import FrameInfo;
import Insets;
import LayoutAlignment;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutStyle;
import NativeKit.Handle;
import NativeKit.SurfaceHandle;
import NativeKitEvents;
import NativeKitSurface;
import Rect;
import Renderer;
import Surface;
import TextStyle;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.HitTest;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.core.WidgetId;
import nativekit.ui.animation.AnimationController;
import nativekit.ui.animation.SpringController;
import nativekit.ui.debug.AccessibilityIssue;
import nativekit.ui.debug.UiNodeSnapshot;
import nativekit.ui.gestures.DoubleTapRecognizer;
import nativekit.ui.gestures.DragRecognizer;
import nativekit.ui.gestures.GestureEvent;
import nativekit.ui.gestures.LongPressRecognizer;
import nativekit.ui.gestures.TapRecognizer;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.theme.Theme;
import nativekit.ui.widgets.Align;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.Checkbox;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.GestureDetector;
import nativekit.ui.widgets.Menu;
import nativekit.ui.widgets.MenuItem;
import nativekit.ui.widgets.Padding;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.Popup;
import nativekit.ui.widgets.RadioGroup;
import nativekit.ui.widgets.RadioOption;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.SizedBox;
import nativekit.ui.widgets.Slider;
import nativekit.ui.widgets.Spacer;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;
import nativekit.ui.widgets.TabItem;
import nativekit.ui.widgets.Tabs;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.TextArea;
import nativekit.ui.widgets.TextEditorState;
import nativekit.ui.widgets.TextField;
import nativekit.ui.widgets.Toggle;
import nativekit.ui.widgets.Tooltip;
import nativekit.ui.widgets.VirtualList;
import ExplorerCatalog;
import shell.CatalogSidebar;
import shell.ExplorerShell;
import inspector.InspectionOverlay;
import inspector.InspectorPanel;
import testing.ExplorerSmokeSequence;
import testing.ExplorerVisualCases;
import components.PageHeader;

/** Interactive, Haxe-composed showcase for NativeKit's UI framework. */
@:allow(pages.GraphicsPage)
@:allow(pages.ControlsPage)
@:allow(pages.GesturesPage)
@:allow(pages.LayoutPage)
@:allow(pages.ListsPage)
@:allow(pages.OverlaysPage)
@:allow(pages.OverviewPage)
@:allow(pages.TextPage)
@:allow(shell.CatalogSidebar)
@:allow(shell.ExplorerShell)
@:allow(shell.TopBar)
@:allow(inspector.InspectionOverlay)
@:allow(inspector.InspectorPanel)
@:allow(ExplorerCatalog)
@:allow(testing.ExplorerVisualCases)
class UiExplorer {
	public static inline var TARGET_FPS:Float = 60.0;
	static inline var LIST_COUNT:Int = 10000;
	static inline var LIST_ROW_HEIGHT:Float = 32.0;

	final context:UiContext;
	final state:ExplorerState;
	final renderer:Renderer;
	final fonts:FontCollection;
	final frame:LayoutFrame;
	final frameInfo:FrameInfo;
	final platformLabel:String;
	final onOpenGraphics:Void->Void;
	final virtualList:VirtualList;
	final tweenController:AnimationController;
	final springController:SpringController;
	var nativeSurface:Null<NativeKitSurface>;
	var width:Float;
	var height:Float;
	var framebufferWidth:Int;
	var framebufferHeight:Int;
	var pixelScale:Float;
	var previousTime:Float = -1.0;
	var frames:Int = 0;
	var diagnosticStage:Int = 0;

	public function new(fonts:FontCollection, platformLabel:String,
			onOpenGraphics:Void->Void) {
		if (fonts == null || fonts.isDisposed())
			throw "UI Explorer requires a live font collection";
		this.fonts = fonts;
		state = new ExplorerState();
		this.platformLabel = platformLabel == null ? "NativeKit runtime" : platformLabel;
		this.onOpenGraphics = onOpenGraphics == null ? function() {} : onOpenGraphics;
		context = new UiContext(null, fonts, makeTheme(false));
		renderer = Renderer.create();
		width = 900.0;
		height = 650.0;
		framebufferWidth = 900;
		framebufferHeight = 650;
		pixelScale = 1.0;
		frame = new LayoutFrame(width, height);
		frameInfo = new FrameInfo(width, height, framebufferWidth, framebufferHeight, pixelScale);
		tweenController = new AnimationController(context.animations, function(value) {
			state.gestures.tweenValue = value;
		});
		springController = new SpringController(state.gestures.springValue, 180.0, 24.0, 1.0, 0.001,
			context.animations, function(value) { state.gestures.springValue = value; });
		var listStyle = new LayoutStyle();
		listStyle.width = LayoutAxis.grow();
		listStyle.height = LayoutAxis.fixed(350.0);
		listStyle.clipVertical = true;
		virtualList = new VirtualList("ten-thousand-rows", LIST_COUNT, LIST_ROW_HEIGHT,
			function(index) {
				var rowStyle = new LayoutStyle();
				rowStyle.width = LayoutAxis.grow();
				rowStyle.height = LayoutAxis.fixed(LIST_ROW_HEIGHT);
				rowStyle.padding = new Insets(8.0, 7.0, 8.0, 7.0);
				rowStyle.background = state.lightTheme
					? (index % 2 == 0 ? color(0.98, 0.99, 1.0) : color(0.91, 0.94, 0.98))
					: (index % 2 == 0 ? color(0.11, 0.14, 0.20) : color(0.13, 0.16, 0.23));
				return new Text('ROW ${index + 1}  ·  virtual item', rowStyle, paletteText());
			}, listStyle, null, state.listController, 350.0);
	}

	/** Installs the native surface used by text editing and platform IME state. */
	public function attachSurface(surface:NativeKitSurface):Void {
		nativeSurface = surface;
		context.attachPlatformSurface(surface);
	}

	/** Routes platform input through the framework's standard NativeKit adapter. */
	public function attachInput(events:NativeKitEvents, window:Handle):NativeInputAdapter {
		var input = new NativeInputAdapter(context, window);
		input.attach(events);
		return input;
	}

	public function setViewport(width:Float, height:Float, framebufferWidth:Int,
			framebufferHeight:Int, pixelScale:Float):Void {
		if (width <= 0.0 || height <= 0.0 || framebufferWidth <= 0 || framebufferHeight <= 0 ||
			pixelScale <= 0.0)
			return;
		this.width = width;
		this.height = height;
		this.framebufferWidth = framebufferWidth;
		this.framebufferHeight = framebufferHeight;
		this.pixelScale = pixelScale;
	}

	/** Selects a page/overlay sequence for deterministic desktop smoke coverage. */
	public function setSmokeFrame(frame:Int):Void {
		ExplorerSmokeSequence.apply(state, frame);
	}

	/** Selects one of the deterministic browser screenshot states, 0-14. */
	public function setVisualCase(caseId:Int):Bool {
		return ExplorerVisualCases.apply(this, caseId);
	}

	public function render(surface:SurfaceHandle, timeSeconds:Float):Void {
		diagnosticStage = 1;
		frame.setViewport(width, height);
		frame.deltaSeconds = previousTime < 0.0 ? 1.0 / TARGET_FPS :
			Math.max(0.0, Math.min(0.1, timeSeconds - previousTime));
		previousTime = timeSeconds;
		frameInfo.set(width, height, framebufferWidth, framebufferHeight, pixelScale);
		diagnosticStage = 2;
		var root = buildRoot();
		diagnosticStage = 3;
		try {
			context.submit(root, frame);
			attachInspectorEvents();
		} catch (error:Dynamic) {
			diagnosticStage = 10 + context.getDiagnosticStage();
			throw error;
		}
		if (state.smokeFocusTextField || state.visualFocusLabel != null) {
			diagnosticStage = 4;
			state.smokeFocusTextField = false;
			var targetLabel = state.visualFocusLabel;
			state.visualFocusLabel = null;
			var selectTextArea = state.visualTextAreaSelection;
			state.visualTextAreaSelection = false;
			var focused = false;
			for (record in context.inspect())
				if (!focused && ((targetLabel != null && record.label == targetLabel) ||
					(targetLabel == null && record.role == AccessibilityRole.TextField))) {
					var widgetId = new WidgetId(record.id);
					focused = context.focusWidget(widgetId);
					if (focused && selectTextArea) {
						var editorState:State<TextEditorState> = context.buildContext.existingState(widgetId);
						var editor:TextEditorState = cast editorState.value;
						if (!editor.setSelection(6, 15))
							throw "UI visual test could not select TextArea text";
						editorState.update(editor);
					}
				}
			if (!focused)
				throw "UI smoke test could not focus a TextField";
		}
		diagnosticStage = 5;
		context.render(renderer, Surface.fromNativeHandle(surface), frameInfo);
		frames++;
		diagnosticStage = 0;
	}

	public function dispose():Void {
		context.dispose();
		renderer.dispose();
		if (nativeSurface != null) {
			nativeSurface.releaseBorrowed();
			nativeSurface = null;
		}
		fonts.dispose();
	}

	public function getDiagnosticStage():Int
		return diagnosticStage;

	function buildRoot():Stack {
		var rootStyle = new LayoutStyle();
		rootStyle.width = LayoutAxis.grow();
		rootStyle.height = LayoutAxis.grow();
		var layers:Array<StackChild> = [new StackChild("explorer-shell", buildShell())];
		if (state.overlays.dialogOpen) {
			var dialog = new Dialog("showcase-dialog", "NativeKit dialog",
				new Column("dialog-content", [
					keyed("copy", text("A modal overlay rendered in the same resolved UI tree.", paletteMuted())),
					keyed("close", button("Done", "dialog-done", function() { state.overlays.dialogOpen = false; }))
				], columnStyle(340.0, 90.0)), function() { state.overlays.dialogOpen = false; }, 390.0);
			layers.push(new StackChild("dialog-layer", dialog, 0.0, 0.0, 30));
		} else if (state.overlays.popupOpen) {
			var popupStyle = panelStyle(250.0);
			var popupContent = new Column("popup-content", [
				keyed("title", text("Quick actions", paletteText())),
				keyed("copy", text("This popup escapes the page clip.", paletteMuted())),
				keyed("dismiss", button("Close popup", "popup-close", function() { state.overlays.popupOpen = false; }))
			], popupStyle);
			var popup = new Popup("showcase-popup", popupContent, Math.max(270.0, width * 0.42), 150.0,
				null, function() { state.overlays.popupOpen = false; });
			popup.label = "Quick actions";
			popup.modal = false;
			layers.push(new StackChild("popup-layer", popup, 0.0, 0.0, 20));
		} else if (state.overlays.menuOpen) {
			var menu = new Menu("showcase-menu", [
				new MenuItem("menu-new", "New document", function() { state.controls.menuSelection = "New document"; }),
				new MenuItem("menu-copy", "Copy selection", function() { state.controls.menuSelection = "Copy selection"; }),
				new MenuItem("menu-disabled", "Unavailable action", null, false)
			], 330.0, 165.0, function() { state.overlays.menuOpen = false; });
			layers.push(new StackChild("menu-layer", menu, 0.0, 0.0, 20));
		}
		InspectionOverlay.addHighlight(this, layers);
		return new Stack("showcase-root", layers, rootStyle);
	}

	function buildShell():Column {
		return ExplorerShell.build(this);
	}

	function buildPage():Column {
		var items:Array<KeyedView> = [];
		var page = ExplorerCatalog.find(state.selectedPage);
		if (page == null)
			page = ExplorerCatalog.find("overview");
		if (page != null)
			page.builder(this, items);
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fit();
		style.childGap = 16.0;
		return new Column("page-content-" + (page == null ? "overview" : page.id), items, style);
	}

	function pageHeading(items:Array<KeyedView>, title:String, description:String):Void {
		PageHeader.append(items, title, description, paletteText(), paletteMuted());
	}

	function buildInspector():Column {
		return InspectorPanel.build(this);
	}

	function attachInspectorEvents():Void {
		InspectionOverlay.attachEvents(this);
	}

	function textField():TextField {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(42.0);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = state.lightTheme ? color(0.92, 0.94, 0.98) : color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new TextField("demo-name", state.controls.nameValue, function(value) {
			state.controls.nameValue = value;
		},
			style, "Display name", new TextStyle(15.0), paletteText());
	}

	function textArea():TextArea {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(146.0);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = state.lightTheme ? color(0.92, 0.94, 0.98) : color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new TextArea("demo-notes", state.controls.notesValue, function(value) {
			state.controls.notesValue = value;
		},
			style, "Multilingual notes", new TextStyle(15.0), paletteText());
	}

	function slider():Slider {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(36.0);
		return new Slider("volume-slider", "Volume", state.controls.volume, 0.0, 1.0, 0.01,
			function(value) { state.controls.volume = value; }, style);
	}

	function button(label:String, key:String, action:Void->Void, selected:Bool = false):Button {
		var style = new LayoutStyle();
		style.height = LayoutAxis.fixed(38.0);
		style.padding = new Insets(12.0, 9.0, 12.0, 9.0);
		style.background = state.lightTheme ? color(0.18, 0.39, 0.70) : color(0.16, 0.38, 0.70);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		var result = new Button(label, style, action, key);
		result.selected = selected;
		return result;
	}

	function disabledButton(label:String):Button {
		var result = button(label, "disabled-demo", function() {});
		result.enabled = false;
		return result;
	}

	function panel(key:String, children:Array<KeyedView>):Column {
		return new Column(key, children, panelStyle());
	}

	function panelStyle(?fixedWidth:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = fixedWidth == null ? LayoutAxis.grow() : LayoutAxis.fixed(fixedWidth);
		style.height = LayoutAxis.fit();
		style.padding = new Insets(16.0, 14.0, 16.0, 14.0);
		style.childGap = 10.0;
		style.background = state.lightTheme ? color(0.97, 0.98, 1.0) : color(0.10, 0.14, 0.22);
		style.radiusTopLeft = style.radiusTopRight = 7.0;
		style.radiusBottomLeft = style.radiusBottomRight = 7.0;
		return style;
	}

	function columnStyle(width:Float, height:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(width);
		style.height = LayoutAxis.fixed(height);
		style.padding = new Insets(20.0, 18.0, 20.0, 18.0);
		style.childGap = 12.0;
		style.background = panelStyle().background;
		return style;
	}

	function rowStyle(gap:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.direction = LayoutDirection.LeftToRight;
		style.childGap = gap;
		return style;
	}

	function fixedBoxStyle(width:Float, height:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(width);
		style.height = LayoutAxis.fixed(height);
		style.padding = new Insets(10.0, 10.0, 10.0, 10.0);
		style.background = state.lightTheme ? color(0.88, 0.91, 0.96) : color(0.07, 0.10, 0.16);
		return style;
	}

	function colorTile(label:String, background:Color):View {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(50.0);
		style.padding = new Insets(10.0, 10.0, 10.0, 10.0);
		style.background = background;
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new Padding("tile-padding", text(label, color(1.0, 1.0, 1.0)),
			new Insets(10.0, 10.0, 10.0, 10.0), style);
	}

	function stackDemo():View {
		var rootStyle = fixedBoxStyle(520.0, 122.0);
		rootStyle.width = LayoutAxis.grow();
		var layers:Array<StackChild> = [
			new StackChild("base", colorTile("base layer", color(0.16, 0.29, 0.45)), 12.0, 12.0, 0,
				LayoutAxis.fixed(250.0), LayoutAxis.fixed(72.0)),
			new StackChild("top", colorTile("z-index 1", color(0.38, 0.26, 0.61)), 190.0, 36.0, 1,
				LayoutAxis.fixed(220.0), LayoutAxis.fixed(68.0))
		];
		return new Stack("positioned-stack", layers, rootStyle);
	}

	function keyed(key:String, view:View):KeyedView
		return new KeyedView(key, view);

	function text(value:String, color:Color):Text
		return new Text(value, null, color);

	function paletteBackground():Color
		return state.lightTheme ? color(0.93, 0.95, 0.98) : color(0.065, 0.085, 0.13);

	function paletteSidebar():Color
		return state.lightTheme ? color(0.88, 0.91, 0.96) : color(0.08, 0.11, 0.17);

	function paletteText():Color
		return state.lightTheme ? color(0.10, 0.14, 0.21) : color(0.91, 0.94, 0.98);

	function paletteMuted():Color
		return state.lightTheme ? color(0.32, 0.38, 0.47) : color(0.62, 0.68, 0.77);

	static function makeTheme(light:Bool):Theme {
		var theme = new Theme();
		theme.accent = light ? color(0.12, 0.37, 0.72) : color(0.25, 0.61, 0.89);
		theme.text = light ? color(0.10, 0.14, 0.21) : color(0.91, 0.94, 0.98);
		theme.mutedText = light ? color(0.32, 0.38, 0.47) : color(0.62, 0.68, 0.77);
		theme.buttonText = color(1.0, 1.0, 1.0);
		theme.disabledButtonText = light ? color(0.38, 0.41, 0.46) : color(0.53, 0.55, 0.59);
		theme.buttonHover = light ? color(0.16, 0.38, 0.69) : color(0.22, 0.48, 0.82);
		theme.buttonPressed = light ? color(0.11, 0.29, 0.54) : color(0.13, 0.34, 0.67);
		theme.buttonFocused = light ? color(0.22, 0.43, 0.73) : color(0.27, 0.52, 0.91);
		theme.buttonSelected = light ? color(0.16, 0.36, 0.65) : color(0.17, 0.37, 0.68);
		theme.buttonDisabled = light ? color(0.82, 0.84, 0.88) : color(0.22, 0.24, 0.28);
		theme.controlSelected = theme.accent;
		theme.controlUnselected = light ? color(0.78, 0.81, 0.86) : color(0.16, 0.18, 0.22);
		theme.controlDisabled = light ? color(0.82, 0.84, 0.88) : color(0.20, 0.21, 0.24);
		theme.panelBackground = light ? color(0.98, 0.98, 1.0) : color(0.14, 0.16, 0.20);
		theme.overlayBackdrop = color(0.0, 0.0, 0.0, 0.54);
		theme.tooltipBackground = light ? color(0.13, 0.17, 0.23) : color(0.08, 0.09, 0.11);
		return theme;
	}

	static inline function color(red:Float, green:Float, blue:Float, alpha:Float = 1.0):Color
		return Color.rgba(red, green, blue, alpha);

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;

	static function containsInsensitive(value:String, needle:String):Bool {
		if (needle.length == 0)
			return true;
		if (needle.length > value.length)
			return false;
		for (start in 0...(value.length - needle.length + 1)) {
			var match = true;
			for (offset in 0...needle.length) {
				if (lowerAscii(value.charCodeAt(start + offset)) != lowerAscii(needle.charCodeAt(offset))) {
					match = false;
					break;
				}
			}
			if (match)
				return true;
		}
		return false;
	}

	static inline function lowerAscii(code:Int):Int
		return code >= 65 && code <= 90 ? code + 32 : code;
}
