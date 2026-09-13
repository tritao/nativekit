import Color;
import FontCollection;
import FrameInfo;
import Insets;
import LayoutAlignment;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutStyle;
import NativeKit.Handle;
import NativeKitEvents;
import NativeKitSurface;
import Renderer;
import Surface;
import TextStyle;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.View;
import nativekit.ui.core.WidgetId;
import nativekit.ui.animation.AnimationController;
import nativekit.ui.animation.SpringController;
import nativekit.ui.debug.AccessibilityIssue;
import nativekit.ui.debug.UiNodeSnapshot;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.theme.Theme;
import nativekit.ui.widgets.Align;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Checkbox;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.KeyedView;
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
import nativekit.ui.widgets.TextField;
import nativekit.ui.widgets.Toggle;
import nativekit.ui.widgets.Tooltip;
import nativekit.ui.widgets.VirtualList;

/** Interactive, Haxe-composed showcase for NativeKit's UI framework. */
class UiExplorer {
	public static inline var TARGET_FPS:Float = 60.0;
	static inline var LIST_COUNT:Int = 10000;
	static inline var LIST_ROW_HEIGHT:Float = 32.0;

	final context:UiContext;
	final renderer:Renderer;
	final fonts:FontCollection;
	final frame:LayoutFrame;
	final frameInfo:FrameInfo;
	final platformLabel:String;
	final onOpenGraphics:Void->Void;
	final listController:ScrollController;
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
	var lightTheme:Bool = false;
	var inspectorOpen:Bool = true;
	var showDialog:Bool = false;
	var showPopup:Bool = false;
	var showMenu:Bool = false;
	var selectedPage:String = "overview";
	var searchText:String = "";
	var nameValue:String = "NativeKit UI";
	var notesValue:String = "مرحبا NativeKit — שלום — こんにちは 👋";
	var checked:Bool = true;
	var enabled:Bool = true;
	var volume:Float = 0.68;
	var progress:Float = 0.72;
	var radioValue:String = "comfortable";
	var selectedTab:String = "preview";
	var menuSelection:String = "No command selected";
	var tweenValue:Float = 0.0;
	var springValue:Float = 0.18;
	var smokeFocusTextField:Bool = false;
	var visualFocusLabel:Null<String>;

	public function new(fonts:FontCollection, platformLabel:String,
			onOpenGraphics:Void->Void) {
		if (fonts == null || fonts.isDisposed())
			throw "UI Explorer requires a live font collection";
		this.fonts = fonts;
		visualFocusLabel = null;
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
		listController = new ScrollController();
		tweenController = new AnimationController(context.animations, function(value) {
			tweenValue = value;
		});
		springController = new SpringController(springValue, 180.0, 24.0, 1.0, 0.001,
			context.animations, function(value) { springValue = value; });
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
				rowStyle.background = index % 2 == 0 ? color(0.11, 0.14, 0.20) : color(0.13, 0.16, 0.23);
				return new Text('ROW ${index + 1}  ·  virtual item', rowStyle, color(0.83, 0.87, 0.94));
			}, listStyle, null, listController, 350.0);
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
		showDialog = false;
		showPopup = false;
		showMenu = false;
		smokeFocusTextField = false;
		switch frame % 11 {
			case 0: selectedPage = "overview";
			case 1: selectedPage = "controls";
			case 2: selectedPage = "text";
			case 3: selectedPage = "text"; smokeFocusTextField = true;
			case 4: selectedPage = "layout";
			case 5: selectedPage = "lists";
			case 6: selectedPage = "overlays";
			case 7: selectedPage = "overlays"; showDialog = true;
			case 8: selectedPage = "overlays"; showPopup = true;
			case 9: selectedPage = "overlays"; showMenu = true;
			default: selectedPage = "graphics";
		}
	}

	/** Selects one of the deterministic browser screenshot states, 0-10. */
	public function setVisualCase(caseId:Int):Bool {
		showDialog = false;
		showPopup = false;
		showMenu = false;
		searchText = "";
		inspectorOpen = true;
		lightTheme = false;
		context.setTheme(makeTheme(false));
		checked = true;
		enabled = true;
		volume = 0.68;
		progress = 0.72;
		radioValue = "comfortable";
		selectedTab = "preview";
		nameValue = "NativeKit UI";
		notesValue = "مرحبا NativeKit — שלום — こんにちは 👋";
		menuSelection = "No command selected";
		listController.jumpTo(0.0, 0.0);
		visualFocusLabel = null;
		selectedPage = "overview";
		switch caseId {
			case 0: selectedPage = "overview";
			case 1: selectedPage = "controls";
			case 2:
				selectedPage = "controls";
				lightTheme = true;
				context.setTheme(makeTheme(true));
			case 3:
				selectedPage = "controls";
				visualFocusLabel = "Primary action";
			case 4:
				selectedPage = "text";
				visualFocusLabel = "Display name";
			case 5: selectedPage = "layout";
			case 6:
				selectedPage = "lists";
				listController.jumpTo(0.0, 414.0 * LIST_ROW_HEIGHT);
			case 7: selectedPage = "overlays"; showDialog = true;
			case 8: selectedPage = "overlays"; showPopup = true;
			case 9: selectedPage = "overlays"; showMenu = true;
			case 10: selectedPage = "controls";
			default: return false;
		}
		return true;
	}

	public function render(surface:Handle, timeSeconds:Float):Void {
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
		} catch (error:Dynamic) {
			diagnosticStage = 10 + context.getDiagnosticStage();
			throw error;
		}
		if (smokeFocusTextField || visualFocusLabel != null) {
			diagnosticStage = 4;
			smokeFocusTextField = false;
			var targetLabel = visualFocusLabel;
			visualFocusLabel = null;
			var focused = false;
			for (record in context.inspect())
				if (!focused && ((targetLabel != null && record.label == targetLabel) ||
					(targetLabel == null && record.role == AccessibilityRole.TextField)))
					focused = context.focusWidget(new WidgetId(record.id));
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
		if (showDialog) {
			var dialog = new Dialog("showcase-dialog", "NativeKit dialog",
				new Column("dialog-content", [
					keyed("copy", text("A modal overlay rendered in the same resolved UI tree.", paletteMuted())),
					keyed("close", button("Done", "dialog-done", function() { showDialog = false; }))
				], columnStyle(340.0, 90.0)), function() { showDialog = false; }, 390.0);
			layers.push(new StackChild("dialog-layer", dialog, 0.0, 0.0, 30));
		} else if (showPopup) {
			var popupStyle = panelStyle(250.0);
			var popupContent = new Column("popup-content", [
				keyed("title", text("Quick actions", paletteText())),
				keyed("copy", text("This popup escapes the page clip.", paletteMuted())),
				keyed("dismiss", button("Close popup", "popup-close", function() { showPopup = false; }))
			], popupStyle);
			var popup = new Popup("showcase-popup", popupContent, Math.max(270.0, width * 0.42), 150.0,
				null, function() { showPopup = false; });
			popup.label = "Quick actions";
			popup.modal = false;
			layers.push(new StackChild("popup-layer", popup, 0.0, 0.0, 20));
		} else if (showMenu) {
			var menu = new Menu("showcase-menu", [
				new MenuItem("menu-new", "New document", function() { menuSelection = "New document"; }),
				new MenuItem("menu-copy", "Copy selection", function() { menuSelection = "Copy selection"; }),
				new MenuItem("menu-disabled", "Unavailable action", null, false)
			], 330.0, 165.0, function() { showMenu = false; });
			layers.push(new StackChild("menu-layer", menu, 0.0, 0.0, 20));
		}
		return new Stack("showcase-root", layers, rootStyle);
	}

	function buildShell():Column {
		var topStyle = new LayoutStyle();
		topStyle.width = LayoutAxis.grow();
		topStyle.height = LayoutAxis.fixed(66.0);
		topStyle.direction = LayoutDirection.LeftToRight;
		topStyle.childAlignY = LayoutAlignment.Center;
		topStyle.padding = new Insets(22.0, 0.0, 22.0, 0.0);
		topStyle.childGap = 12.0;
		topStyle.background = color(0.055, 0.075, 0.12);
		var top = new Row("top-bar", [
			keyed("brand-mark", text("NK", color(0.31, 0.91, 0.72))),
			keyed("brand", text("NativeKit UI Explorer", paletteText())),
			keyed("space", new Spacer("top-spacer", LayoutAxis.grow(), LayoutAxis.fit())),
			keyed("platform", text(platformLabel, paletteMuted())),
			keyed("theme", button(lightTheme ? "Light theme" : "Dark theme", "theme-toggle", function() {
				lightTheme = !lightTheme;
				context.setTheme(makeTheme(lightTheme));
			})),
			keyed("inspect", button(inspectorOpen ? "Hide inspector" : "Inspect", "inspector-toggle", function() {
				inspectorOpen = !inspectorOpen;
			}))
		], topStyle);

		var bodyStyle = new LayoutStyle();
		bodyStyle.width = LayoutAxis.grow();
		bodyStyle.height = LayoutAxis.grow();
		bodyStyle.direction = LayoutDirection.LeftToRight;
		bodyStyle.childGap = 0.0;
		var bodyChildren:Array<KeyedView> = [keyed("catalog", buildCatalog())];
		var mainStyle = new LayoutStyle();
		mainStyle.width = LayoutAxis.grow();
		mainStyle.height = LayoutAxis.grow();
		mainStyle.padding = new Insets(22.0, 18.0, 22.0, 18.0);
		var scrollStyle = new LayoutStyle();
		scrollStyle.width = LayoutAxis.grow();
		scrollStyle.height = LayoutAxis.grow();
		scrollStyle.clipVertical = true;
		var pageScroll = new ScrollView("page-scroll", buildPage(), scrollStyle, ScrollAxis.Vertical);
		bodyChildren.push(keyed("main", new Column("main-content", [keyed("page", pageScroll)], mainStyle)));
		if (inspectorOpen && width >= 880.0)
			bodyChildren.push(keyed("inspector", buildInspector()));
		var body = new Row("workspace", bodyChildren, bodyStyle);
		var shellStyle = new LayoutStyle();
		shellStyle.width = LayoutAxis.grow();
		shellStyle.height = LayoutAxis.grow();
		shellStyle.background = paletteBackground();
		return new Column("app-shell", [keyed("top", top), keyed("workspace", body)], shellStyle);
	}

	function buildCatalog():Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(width < 760.0 ? 176.0 : 212.0);
		style.height = LayoutAxis.grow();
		style.padding = new Insets(14.0, 18.0, 14.0, 18.0);
		style.childGap = 8.0;
		style.background = paletteSidebar();
		var children:Array<KeyedView> = [
			keyed("catalog-label", text("COMPONENT CATALOG", paletteMuted()))
		];
		var searchStyle = new LayoutStyle();
		searchStyle.width = LayoutAxis.grow();
		searchStyle.height = LayoutAxis.fixed(38.0);
		searchStyle.padding = new Insets(9.0, 7.0, 9.0, 7.0);
		searchStyle.background = lightTheme ? color(0.91, 0.93, 0.97) : color(0.09, 0.12, 0.18);
		var search = new TextField("catalog-search", searchText, function(value) {
			searchText = value;
		}, searchStyle, "Search components", new TextStyle(14.0), paletteText());
		children.push(keyed("search", search));
		children.push(keyed("group-start", text("START HERE", paletteMuted())));
		appendNav(children, "overview", "Overview");
		children.push(keyed("group-components", text("COMPONENTS", paletteMuted())));
		appendNav(children, "controls", "Controls");
		appendNav(children, "text", "Text & Input");
		appendNav(children, "layout", "Layout");
		appendNav(children, "lists", "Scrolling & Data");
		appendNav(children, "overlays", "Navigation & Overlays");
		children.push(keyed("group-developer", text("DEVELOPER TOOLS", paletteMuted())));
		appendNav(children, "graphics", "Graphics Lab");
		children.push(keyed("catalog-space", new Spacer("catalog-space", LayoutAxis.fit(), LayoutAxis.grow())));
		children.push(keyed("catalog-foot", text("Haxe composition\nNative layout + render", paletteMuted())));
		return new Column("component-catalog", children, style);
	}

	function appendNav(children:Array<KeyedView>, key:String, label:String):Void {
		if (searchText.length > 0 && !containsInsensitive(label, searchText))
			return;
		var navStyle = new LayoutStyle();
		navStyle.width = LayoutAxis.grow();
		navStyle.height = LayoutAxis.fixed(36.0);
		navStyle.padding = new Insets(10.0, 8.0, 10.0, 8.0);
		navStyle.background = lightTheme ? color(0.87, 0.90, 0.95) : color(0.075, 0.10, 0.16);
		var item = new Button(label, navStyle, function() {
			selectedPage = key;
		}, "nav-" + key);
		item.selected = selectedPage == key;
		children.push(keyed("nav-" + key, item));
	}

	function buildPage():Column {
		var items:Array<KeyedView> = [];
		switch selectedPage {
			case "controls": buildControls(items);
			case "text": buildTextPage(items);
			case "layout": buildLayoutPage(items);
			case "lists": buildListsPage(items);
			case "overlays": buildOverlaysPage(items);
			case "graphics": buildGraphicsPage(items);
			default: buildOverview(items);
		}
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fit();
		style.childGap = 16.0;
		return new Column("page-content-" + selectedPage, items, style);
	}

	function pageHeading(items:Array<KeyedView>, title:String, description:String):Void {
		items.push(keyed("page-title", text(title, paletteText())));
		items.push(keyed("page-description", text(description, paletteMuted())));
	}

	function buildOverview(items:Array<KeyedView>):Void {
		pageHeading(items, "Haxe UI, rendered by NativeKit",
			"A live explorer for composition, native layout, input and rendering.");
		var cards = new Row("overview-cards", [
			keyed("platform-card", card("platform-card", "PLATFORM", platformLabel,
				"NativeKit owns the window, input and graphics surface.")),
			keyed("composition-card", card("composition-card", "COMPOSITION", "Haxe widgets",
				"Stable widget IDs keep focus and state across frames.")),
			keyed("layout-card", card("layout-card", "LAYOUT", "Native engine",
				"Submit the tree, resolve bounds, then render the result."))
		], rowStyle(0.0));
		items.push(keyed("overview-cards", cards));
		items.push(keyed("overview-quick-start", panel("quick-start", [
			keyed("quick-title", text("Try the framework", paletteText())),
			keyed("quick-copy", text("Switch pages from the catalog. Edit the multilingual text field, tab through controls, scroll the virtual list, or open the live inspector.", paletteMuted())),
			keyed("quick-controls", new Row("quick-controls", [
				keyed("toggle", new Toggle("overview-toggle", "Enable preview", enabled, function(value) { enabled = value; })),
				keyed("progress", new ProgressBar("overview-progress", progress, 0.0, 1.0, "Preview progress"))
			], rowStyle(18.0)))
		])));
		items.push(keyed("overview-pipeline", panel("pipeline", [
			keyed("pipeline-title", text("One application, two runtimes", paletteText())),
			keyed("pipeline-copy", text("NativeKit provides platform, IME and graphics services. Haxe owns the UI tree, state and semantics. The same showcase runs on desktop and WebAssembly.", paletteMuted()))
		])));
		items.push(keyed("overview-motion", panel("motion", [
			keyed("motion-title", text("Haxe-owned motion", paletteText())),
			keyed("motion-copy", text('Tween ${Std.int(tweenValue * 100)}%  ·  Spring ${Std.int(springValue * 100)}%', paletteMuted())),
			keyed("motion-actions", new Row("motion-actions", [
				keyed("play-tween", button("Play tween", "play-tween", function() {
					tweenController.play(0.0, 1.0, 0.8);
				})),
				keyed("play-spring", button("Spring bounce", "play-spring", function() {
					springController.setTarget(springValue < 0.5 ? 1.0 : 0.18);
				}))
			], rowStyle(10.0)))
		])));
	}

	function buildControls(items:Array<KeyedView>):Void {
		pageHeading(items, "Controls", "Focus, hover, pressed, selected and disabled states are part of each widget.");
		items.push(keyed("controls-row", new Row("controls-row", [
			keyed("buttons", panel("buttons-card", [
				keyed("heading", text("Buttons", paletteText())),
				keyed("primary", button("Primary action", "primary-action", function() { progress = Math.min(1.0, progress + 0.08); })),
				keyed("secondary", button("Selected", "selected-action", function() {}, true)),
				keyed("disabled", disabledButton("Disabled action")),
				keyed("hint", text("Tab to focus · Enter to activate", paletteMuted()))
			])),
			keyed("selection", panel("selection-card", [
				keyed("heading", text("Selection", paletteText())),
				keyed("checkbox", new Checkbox("show-labels", "Show labels", checked, function(value) { checked = value; })),
				keyed("toggle", new Toggle("control-enabled", "Live updates", enabled, function(value) { enabled = value; })),
				keyed("radio", new RadioGroup("density", [
					new RadioOption("comfortable", "Comfortable", "comfortable"),
					new RadioOption("compact", "Compact", "compact"),
					new RadioOption("disabled", "Unavailable", "disabled", false)
				], radioValue, function(value) { radioValue = value; }))
			]))
		], rowStyle(14.0))));
		items.push(keyed("controls-range", panel("range-card", [
			keyed("heading", text("Range and progress", paletteText())),
			keyed("slider-label", text('Opacity / volume  ·  ${Std.int(volume * 100)}%', paletteMuted())),
			keyed("slider", slider()),
			keyed("progress-label", text('Progress  ·  ${Std.int(progress * 100)}%', paletteMuted())),
			keyed("progress", new ProgressBar("showcase-progress", progress, 0.0, 1.0, "Task progress")),
			keyed("advance", button("Advance progress", "advance-progress", function() {
				progress = progress >= 1.0 ? 0.0 : Math.min(1.0, progress + 0.1);
			}))
		])));
	}

	function buildTextPage(items:Array<KeyedView>):Void {
		pageHeading(items, "Text & Input", "Native text shaping, selection and keyboard editing, with platform IME composition where available.");
		items.push(keyed("text-input-row", new Row("text-input-row", [
			keyed("single-line", panel("text-field-card", [
				keyed("heading", text("TextField", paletteText())),
				keyed("copy", text("Type, select, paste and move the caret with the keyboard.", paletteMuted())),
				keyed("field", textField()),
				keyed("value", text('Current value: ${nameValue}', paletteMuted()))
			])),
			keyed("multiline", panel("text-area-card", [
				keyed("heading", text("TextArea + IME", paletteText())),
				keyed("copy", text("Compose accented text or switch to an RTL / CJK keyboard.", paletteMuted())),
				keyed("area", textArea()),
				keyed("sample", text("مرحبا NativeKit  ·  שלום  ·  こんにちは  ·  👋", color(0.40, 0.83, 0.87)))
			]))
		], rowStyle(14.0))));
		items.push(keyed("text-architecture", panel("text-architecture", [
			keyed("heading", text("Platform text-input status", paletteText())),
			keyed("copy", text(textInputStatus(), paletteMuted()))
		])));
	}

	function textInputStatus():String {
		if (context.textInput.platformChecked && !context.textInput.platformSupported)
			return "This NativeKit backend does not provide custom IME state. The editor remains active for delivered Unicode text input, but composition updates may be unavailable.";
		if (context.textInput.platformSupported)
			return "The focused editor publishes selection, composition range and caret bounds to NativeKit; the host returns committed text and composition updates through the UI event path.";
		return "Focus a text field to check custom IME support. Unsupported backends keep text editing active, while composition updates may be unavailable.";
	}

	function buildLayoutPage(items:Array<KeyedView>):Void {
		pageHeading(items, "Layout", "Resize the window to watch the native layout transaction reflow these Haxe compositions.");
		items.push(keyed("layout-primitives", new Row("layout-primitives", [
			keyed("row-column", panel("row-column", [
				keyed("heading", text("Row + Column", paletteText())),
				keyed("copy", text("Fixed gaps and grow sizing", paletteMuted())),
				keyed("row", new Row("sample-row", [
					keyed("one", colorTile("One", color(0.24, 0.48, 0.77))),
					keyed("two", colorTile("Two", color(0.39, 0.34, 0.72))),
					keyed("three", colorTile("Three", color(0.20, 0.58, 0.52)))
				], rowStyle(8.0)))
			])),
			keyed("padding-align", panel("padding-align", [
				keyed("heading", text("Padding + Align", paletteText())),
				keyed("aligned", new Align("centered-content",
					colorTile("Centered in a padded box", color(0.30, 0.40, 0.59)),
					LayoutAlignment.Center, LayoutAlignment.Center, fixedBoxStyle(270.0, 90.0)))
			]))
		], rowStyle(14.0))));
		items.push(keyed("layout-stack", panel("stack-demo", [
			keyed("heading", text("Stack + clipping", paletteText())),
			keyed("copy", text("Positioned children paint by z-index and inherit their parent's clip.", paletteMuted())),
			keyed("stack", stackDemo())
		])));
	}

	function buildListsPage(items:Array<KeyedView>):Void {
		pageHeading(items, "Scrolling & Data", "A fixed-row VirtualList with a real 10,000-item data set and explicit visible-range reporting.");
		var first = Std.int(listController.offsetY / LIST_ROW_HEIGHT) + 1;
		var last = Std.int((listController.offsetY + Math.max(0.0, listController.viewportHeight)) /
			LIST_ROW_HEIGHT) + 1;
		if (last > LIST_COUNT)
			last = LIST_COUNT;
		if (first > LIST_COUNT)
			first = LIST_COUNT;
		items.push(keyed("list-status", panel("list-status", [
			keyed("status", text('Rendering rows ${first}–${last} / ${LIST_COUNT}', color(0.35, 0.85, 0.69))),
			keyed("copy", text("Scroll inside the list. Only the viewport window and a small overscan are built as Haxe widgets.", paletteMuted())),
			keyed("jump", button("Jump to row 415", "jump-row", function() {
				listController.jumpTo(0.0, 414.0 * LIST_ROW_HEIGHT);
			}))
		])));
		items.push(keyed("virtual-list", virtualList));
	}

	function buildOverlaysPage(items:Array<KeyedView>):Void {
		pageHeading(items, "Navigation & Overlays", "Tabs, menus, popups, tooltips and dialogs share focus, clipping and z-order rules.");
		items.push(keyed("tabs-card", panel("tabs-card", [
			keyed("heading", text("Tabs", paletteText())),
			keyed("tabs", new Tabs("demo-tabs", [
				new TabItem("preview", "Preview", text("Selected content is built lazily and keeps a stable keyed identity.", paletteMuted())),
				new TabItem("details", "Details", text("Tab navigation supports arrow keys and visible focus.", paletteMuted())),
				new TabItem("disabled", "Disabled", text("This tab is not enabled.", paletteMuted()), false)
			], selectedTab, function(value) { selectedTab = value; }))
		])));
		items.push(keyed("overlay-actions", panel("overlay-actions", [
			keyed("heading", text("Open an overlay", paletteText())),
			keyed("actions", new Row("overlay-buttons", [
				keyed("dialog", button("Show dialog", "show-dialog", function() { showDialog = true; })),
				keyed("popup", button("Show popup", "show-popup", function() { showPopup = true; })),
				keyed("menu", button("Show menu", "show-menu", function() { showMenu = true; }))
			], rowStyle(10.0))),
			keyed("menu-state", text(menuSelection, paletteMuted())),
			keyed("tooltip", new Tooltip("tooltip-demo",
				button("Hover for tooltip", "tooltip-anchor", function() {}),
				text("Tooltip content is positioned in a Stack layer.", paletteText()), 0.0, -32.0))
		])));
	}

	function buildGraphicsPage(items:Array<KeyedView>):Void {
		pageHeading(items, "Graphics Lab", "The original retained graphics demonstration remains part of this showcase.");
		items.push(keyed("graphics-card", panel("graphics-card", [
			keyed("heading", text("Paths · text · images · offscreen rendering", paletteText())),
			keyed("copy", text("Explore Bézier paths and stroke joins, multilingual shaping and caret hit testing, clipped image layers, retained display lists, and the depth-tested cube. The full graphics canvas keeps its existing renderer and deterministic visual tests.", paletteMuted())),
			keyed("launch", button("Open full Graphics Lab", "open-graphics-lab", onOpenGraphics)),
			keyed("hint", text("Press Escape in the Graphics Lab to return here.", paletteMuted()))
		])));
		items.push(keyed("graphics-pipeline", panel("graphics-pipeline", [
			keyed("heading", text("Graphics is one page in the explorer", paletteText())),
			keyed("copy", text("The catalog shell and framework demos use UiContext.submit → native layout → render. The original canvas lab remains available as the focused graphics workload.", paletteMuted()))
		])));
	}

	function buildInspector():Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(270.0);
		style.height = LayoutAxis.grow();
		style.padding = new Insets(16.0, 14.0, 16.0, 14.0);
		style.childGap = 9.0;
		style.background = paletteSidebar();
		var children:Array<KeyedView> = [
			keyed("title", text("LIVE INSPECTOR", paletteText())),
			keyed("subtitle", text("Resolved Haxe render tree", paletteMuted()))
		];
		var records:Array<UiNodeSnapshot> = context.inspect();
		var shown = 0;
		for (record in records) {
			if (record.role < 0 || record.role == AccessibilityRole.Text)
				continue;
			var line = '#${record.id} ${roleName(record.role)}';
			if (record.label != null && record.label.length > 0)
				line += ' · ${record.label}';
			children.push(keyed('tree-node-${shown}', text(line, paletteText())));
			var bounds = '${Std.int(record.bounds.x)},${Std.int(record.bounds.y)} · ' +
				'${Std.int(record.bounds.width)}×${Std.int(record.bounds.height)} · actions=${record.actions}';
			if (record.focused)
				bounds += " · focused";
			children.push(keyed('tree-bounds-${shown}', text(bounds, paletteMuted())));
			shown++;
			if (shown >= 5)
				break;
		}
		if (shown == 0)
			children.push(keyed("tree-empty", text("Waiting for semantic widgets…", paletteMuted())));
		var issues:Array<AccessibilityIssue> = context.auditAccessibility();
		children.push(keyed("audit-title", text("ACCESSIBILITY AUDIT", paletteText())));
		children.push(keyed("audit-status", text(issues.length == 0
			? "No naming or focus-bound issues"
			: '${issues.length} issue(s) found', issues.length == 0
				? color(0.35, 0.85, 0.69) : color(0.96, 0.58, 0.31))));
		if (issues.length > 0) {
			var auditLines:Array<String> = [];
			var issueCount = issues.length < 4 ? issues.length : 4;
			for (index in 0...issueCount)
				auditLines.push(issues[index].code + ": " + issues[index].message);
			children.push(keyed("audit-details", text(auditLines.join("\n"), paletteMuted())));
		}
		children.push(keyed("inspector-hint", text("Tree details refresh with each submitted frame.", paletteMuted())));
		return new Column("inspector-drawer", children, style);
	}

	function textField():TextField {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(42.0);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = lightTheme ? color(0.92, 0.94, 0.98) : color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new TextField("demo-name", nameValue, function(value) { nameValue = value; },
			style, "Display name", new TextStyle(15.0), paletteText());
	}

	function textArea():TextArea {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(146.0);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = lightTheme ? color(0.92, 0.94, 0.98) : color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new TextArea("demo-notes", notesValue, function(value) { notesValue = value; },
			style, "Multilingual notes", new TextStyle(15.0), paletteText());
	}

	function slider():Slider {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(36.0);
		return new Slider("volume-slider", "Volume", volume, 0.0, 1.0, 0.01,
			function(value) { volume = value; }, style);
	}

	function button(label:String, key:String, action:Void->Void, selected:Bool = false):Button {
		var style = new LayoutStyle();
		style.height = LayoutAxis.fixed(38.0);
		style.padding = new Insets(12.0, 9.0, 12.0, 9.0);
		style.background = lightTheme ? color(0.18, 0.39, 0.70) : color(0.16, 0.38, 0.70);
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

	function card(key:String, title:String, value:String, description:String):Column {
		return panel(key, [
			keyed("eyebrow", text(title, color(0.40, 0.74, 0.92))),
			keyed("value", text(value, paletteText())),
			keyed("description", text(description, paletteMuted()))
		]);
	}

	function panelStyle(?fixedWidth:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = fixedWidth == null ? LayoutAxis.grow() : LayoutAxis.fixed(fixedWidth);
		style.height = LayoutAxis.fit();
		style.padding = new Insets(16.0, 14.0, 16.0, 14.0);
		style.childGap = 10.0;
		style.background = lightTheme ? color(0.97, 0.98, 1.0) : color(0.10, 0.14, 0.22);
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
		style.background = lightTheme ? color(0.88, 0.91, 0.96) : color(0.07, 0.10, 0.16);
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
		return lightTheme ? color(0.93, 0.95, 0.98) : color(0.065, 0.085, 0.13);

	function paletteSidebar():Color
		return lightTheme ? color(0.88, 0.91, 0.96) : color(0.08, 0.11, 0.17);

	function paletteText():Color
		return lightTheme ? color(0.10, 0.14, 0.21) : color(0.91, 0.94, 0.98);

	function paletteMuted():Color
		return lightTheme ? color(0.32, 0.38, 0.47) : color(0.62, 0.68, 0.77);

	static function roleName(role:Int):String {
		return switch role {
			case 0: "Group";
			case 1: "Button";
			case 2: "Checkbox";
			case 3: "Radio";
			case 5: "TextField";
			case 6: "Link";
			case 7: "Image";
			case 8: "Heading";
			case 9: "List";
			case 10: "ListItem";
			case 11: "Slider";
			case 12: "ScrollArea";
			default: "Text";
		}
	}

	static function makeTheme(light:Bool):Theme {
		var theme = new Theme();
		theme.accent = light ? color(0.12, 0.37, 0.72) : color(0.25, 0.61, 0.89);
		theme.text = light ? color(0.10, 0.14, 0.21) : color(0.91, 0.94, 0.98);
		theme.mutedText = light ? color(0.32, 0.38, 0.47) : color(0.62, 0.68, 0.77);
		theme.buttonHover = light ? color(0.22, 0.47, 0.82) : color(0.22, 0.48, 0.82);
		theme.buttonPressed = light ? color(0.13, 0.33, 0.62) : color(0.13, 0.34, 0.67);
		theme.buttonFocused = light ? color(0.27, 0.52, 0.84) : color(0.27, 0.52, 0.91);
		theme.buttonSelected = light ? color(0.74, 0.83, 0.95) : color(0.17, 0.37, 0.68);
		theme.buttonDisabled = light ? color(0.78, 0.81, 0.86) : color(0.22, 0.24, 0.28);
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
