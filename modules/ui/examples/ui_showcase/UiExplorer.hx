import Color;
import FontCollection;
import Image;
import ImageFormat;
import ImageFilter;
import haxe.io.Bytes;
import FrameInfo;
import LayoutAxis;
import LayoutFrame;
import LayoutStyle;
import NativeKit.Handle;
import NativeKit.WindowHandle;
import NativeKit.Capabilities;
import NativeKit.WindowDecorationRegionKind;
import NativeKit.SurfaceHandle;
import NativeKit.WebviewOptions;
import NativeKitEvents;
import NativeKitSurface;
import NativeKitWebView;
import Renderer;
import Surface;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.debug.UiFrameMetrics;
import nativekit.ui.core.View;
import nativekit.ui.animation.AnimationController;
import nativekit.ui.animation.SpringController;
import nativekit.ui.theme.Theme;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.DefaultTextStyle;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Slider;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.TextArea;
import nativekit.ui.widgets.TextEditorDiagnostics;
import nativekit.ui.widgets.TextField;
import nativekit.ui.widgets.SizedBox;
import nativekit.ui.core.TextStyleOverride;
import TextWrap;
import nativekit.ui.widgets.VirtualList;
import nativekit.ui.widgets.WindowChrome;
import ExplorerCatalog;
import pages.EffectsPage;
import shell.ExplorerShell;
import shell.OverlayHost;
import inspector.InspectionOverlay;
import inspector.InspectorPanel;
import testing.ExplorerSmokeSequence;
import testing.ExplorerFocusSequence;
import testing.ExplorerVisualCases;
import components.PageHeader;
import components.ShowcaseKit;
import components.WebViewSlot;
import pages.ListsPage;

/** Interactive showcase for the Haxeon UI framework. */
@:allow(pages.GraphicsPage)
@:allow(pages.ControlsPage)
@:allow(pages.EffectsPage)
@:allow(pages.GesturesPage)
@:allow(pages.LayoutPage)
@:allow(pages.ListsPage)
@:allow(pages.OverlaysPage)
@:allow(pages.OverviewPage)
@:allow(pages.TextPage)
@:allow(pages.WebViewPage)
@:allow(shell.CatalogSidebar)
@:allow(shell.ExplorerShell)
@:allow(shell.TopBar)
@:allow(inspector.InspectionOverlay)
@:allow(inspector.InspectorPanel)
@:allow(ExplorerCatalog)
@:allow(testing.ExplorerVisualCases)
@:allow(testing.ExplorerFocusSequence)
@:allow(components.ShowcaseKit)
@:allow(shell.OverlayHost)
class UiExplorer {
	public static inline var TARGET_FPS:Float = 60.0;
	public static inline var INITIAL_WIDTH:Int = 1320;
	public static inline var INITIAL_HEIGHT:Int = 900;
	static inline var WINDOW_RESIZE_EDGE:Float = 6.0;
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
	final staticSubmitReuse:Bool;
	final demoImage:Image;
	final demoOverlayImage:Image;
	final demoNineSliceImage:Image;
	final demoLoadedImage:Image;
	final demoPixelLinear:Image;
	final demoPixelNearest:Image;
	var nativeSurface:Null<NativeKitSurface>;
	var nativeWindow:Null<Handle>;
	var webView:Null<NativeKitWebView>;
	var webViewAttempted:Bool = false;
	var webViewSupported:Bool = false;
	var webViewVisible:Bool = false;
	var webViewX:Int = -1;
	var webViewY:Int = -1;
	var webViewWidth:Int = -1;
	var webViewHeight:Int = -1;
	var width:Float;
	var height:Float;
	var framebufferWidth:Int;
	var framebufferHeight:Int;
	var pixelScale:Float;
	var previousTime:Float = -1.0;
	var frames:Int = 0;
	var diagnosticStage:Int = 0;

	public function new(fonts:FontCollection, platformLabel:String,
			onOpenGraphics:Void->Void, ?staticSubmitReuse:Bool, ?demoImagePath:String) {
		if (fonts == null || fonts.isDisposed())
			throw "UI Explorer requires a live font collection";
		this.fonts = fonts;
		state = new ExplorerState();
		this.platformLabel = platformLabel == null ? "NativeKit runtime" : platformLabel;
		this.onOpenGraphics = onOpenGraphics == null ? function() {} : onOpenGraphics;
		this.staticSubmitReuse = staticSubmitReuse == true;
		context = new UiContext(null, fonts, makeTheme(state.lightTheme));
		EffectsPage.installStyles(this);
		renderer = Renderer.create();
		width = INITIAL_WIDTH;
		height = INITIAL_HEIGHT;
		framebufferWidth = INITIAL_WIDTH;
		framebufferHeight = INITIAL_HEIGHT;
		pixelScale = 1.0;
		frame = new LayoutFrame(width, height);
		frameInfo = new FrameInfo(width, height, framebufferWidth, framebufferHeight, pixelScale);
		virtualList = ListsPage.createVirtualList(this);
		tweenController = new AnimationController(context.animations, function(value) {
			state.gestures.tweenValue = value;
		});
		springController = new SpringController(state.gestures.springValue, 180.0, 24.0, 1.0, 0.001,
			context.animations, function(value) { state.gestures.springValue = value; });
		demoImage = createDemoImage(160, 96);
		demoOverlayImage = createOverlayImage(64);
		demoNineSliceImage = createNineSliceImage(48);
		demoLoadedImage = loadDemoImage(demoImagePath);
		demoPixelLinear = createPixelImage(ImageFilter.Linear);
		demoPixelNearest = createPixelImage(ImageFilter.Nearest);
	}

	/** Installs the native surface used by text editing, IME state, and accessibility. */
	public function attachSurface(surface:NativeKitSurface):Void {
		nativeSurface = surface;
		context.attachPlatformSurface(surface);
		if (supportsNativeAccessibility())
			context.updateAccessibility(surface);
	}

	/** Installs the desktop window that receives the shell's custom chrome regions. */
	public function attachWindow(window:WindowHandle):Void
		context.attachPlatformWindow(window);

	/** Routes platform input through the framework's standard NativeKit adapter. */
	public function attachInput(events:NativeKitEvents, window:Handle):NativeInputAdapter {
		nativeWindow = window;
		webViewSupported = supportsCapability(Capabilities.webview());
		var accessibilitySource = nativeSurface == null || !supportsNativeAccessibility() ? window :
			new Handle(nativeSurface.nativeHandle().rawValue());
		var input = new NativeInputAdapter(context, window, accessibilitySource);
		input.attach(events);
		return input;
	}

	function supportsNativeAccessibility():Bool
		return supportsCapability(Capabilities.accessibility());

	function supportsCapability(capability:haxe.Int64):Bool
		return haxe.Int64.compare(haxe.Int64.and(NativeKit.nk_get_capabilities(), capability),
			haxe.Int64.ofInt(0)) != 0;

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

	/** Selects one of the deterministic browser screenshot states, 0-23. */
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
		diagnosticStage = 3;
		try {
			if (staticSubmitReuse)
				context.submitCached(function() return buildRoot(), frame, "showcase-static-controls");
			else
				context.submit(buildRoot(), frame);
			var metrics = context.frameMetrics;
			if (metrics == null || !metrics.reusedSubmission)
				attachInspectorEvents();
		} catch (error:Dynamic) {
			diagnosticStage = 10 + context.getDiagnosticStage();
			throw error;
		}
		ExplorerFocusSequence.applyAfterSubmit(this);
		applySmokeSelectState();
		syncWebView();
		diagnosticStage = 5;
		try {
			context.render(renderer, Surface.fromNativeHandle(surface), frameInfo);
		} catch (error:Dynamic) {
			diagnosticStage = 30 + context.getDiagnosticStage();
			throw error;
		}
		frames++;
		diagnosticStage = 0;
	}

	public function dispose():Void {
		if (webView != null) {
			webView.dispose();
			webView = null;
		}
		context.dispose();
		renderer.dispose();
		demoNineSliceImage.dispose();
		demoPixelNearest.dispose();
		demoPixelLinear.dispose();
		demoLoadedImage.dispose();
		demoOverlayImage.dispose();
		demoImage.dispose();
		if (nativeSurface != null) {
			nativeSurface.releaseBorrowed();
			nativeSurface = null;
		}
		fonts.dispose();
	}

	function webViewAvailable():Bool
		return webViewSupported;

	function reloadWebView():Void {
		ensureWebView();
		if (webView != null)
			webView.setHtml(webViewDemoHtml());
	}

	function ensureWebView():Void {
		if (webView != null || webViewAttempted || !webViewSupported || nativeWindow == null)
			return;
		webViewAttempted = true;
		try {
			var options = new WebviewOptions();
			options.set_x(-10000);
			options.set_y(-10000);
			options.set_width(1);
			options.set_height(1);
			webView = NativeKitWebView.create(nativeWindow, options);
			webView.show(false);
			webView.setHtml(webViewDemoHtml());
		} catch (_:Dynamic) {
			webView = null;
		}
	}

	function syncWebView():Void {
		var shouldShow = state.selectedPage == "webview" && !state.inspector.open &&
			!state.overlays.dialogOpen && !state.overlays.popupOpen && !state.overlays.menuOpen;
		if (!shouldShow) {
			setWebViewVisible(false);
			return;
		}
		ensureWebView();
		if (webView == null)
			return;
		for (record in context.inspect()) {
			if (record.label != WebViewSlot.SemanticLabel || !record.visible)
				continue;
			var left = Math.max(record.bounds.x, record.clipBounds.x);
			var top = Math.max(record.bounds.y, record.clipBounds.y);
			var right = Math.min(record.bounds.x + record.bounds.width,
				record.clipBounds.x + record.clipBounds.width);
			var bottom = Math.min(record.bounds.y + record.bounds.height,
				record.clipBounds.y + record.clipBounds.height);
			var viewWidth = Std.int(Math.max(0.0, right - left));
			var viewHeight = Std.int(Math.max(0.0, bottom - top));
			var fullyVisible = Math.abs(left - record.bounds.x) < 0.5 &&
				Math.abs(top - record.bounds.y) < 0.5 &&
				Math.abs(right - record.bounds.x - record.bounds.width) < 0.5 &&
				Math.abs(bottom - record.bounds.y - record.bounds.height) < 0.5;
			if (!fullyVisible || viewWidth < 2 || viewHeight < 2) {
				setWebViewVisible(false);
				return;
			}
			var viewX = Std.int(left);
			var viewY = Std.int(top);
			if (viewX != webViewX || viewY != webViewY || viewWidth != webViewWidth ||
					viewHeight != webViewHeight) {
				webView.setBounds(viewX, viewY, viewWidth, viewHeight);
				webViewX = viewX;
				webViewY = viewY;
				webViewWidth = viewWidth;
				webViewHeight = viewHeight;
			}
			setWebViewVisible(true);
			return;
		}
		setWebViewVisible(false);
	}

	function setWebViewVisible(visible:Bool):Void {
		if (webView == null || webViewVisible == visible)
			return;
		webView.show(visible);
		webViewVisible = visible;
	}

	static function webViewDemoHtml():String
		return '<!doctype html><html><head><meta charset="utf-8"><style>
			body{margin:0;background:#101827;color:#e9f1ff;font:16px system-ui;display:grid;place-items:center;height:100vh}
			main{max-width:620px;padding:36px} small{color:#67c7ff;text-transform:uppercase;letter-spacing:.12em}
			h1{font-size:38px;margin:10px 0} p{color:#aebdd2;line-height:1.6}
			button{border:0;border-radius:7px;background:#2f6dcc;color:white;padding:11px 16px;font:inherit}
			#count{display:inline-block;min-width:2ch;font-weight:700}
		</style></head><body><main><small>Native child view</small><h1>Web content inside Haxeon</h1>
		<p>This HTML is rendered by the platform WebView while Haxeon controls the surrounding layout and lifecycle.</p>
		<button onclick="document.getElementById(\'count\').textContent=+document.getElementById(\'count\').textContent+1">
		Interactive count: <span id="count">0</span></button></main></body></html>';

	static function createDemoImage(width:Int, height:Int):Image {
		var pixels = Bytes.alloc(width * height * 4);
		for (y in 0...height)
			for (x in 0...width) {
				var horizon = y < Std.int(height * 0.58);
				var stripe = ((x + y) % 24) < 12;
				var red = horizon ? 45 + Std.int(50 * y / height) : (stripe ? 36 : 49);
				var green = horizon ? 105 + Std.int(80 * y / height) : (stripe ? 122 : 145);
				var blue = horizon ? 190 + Std.int(45 * y / height) : (stripe ? 82 : 96);
				var sunX = x - Std.int(width * 0.72);
				var sunY = y - Std.int(height * 0.28);
				if (sunX * sunX + sunY * sunY < 13 * 13) {
					red = 255; green = 190; blue = 72;
				}
				var offset = (y * width + x) * 4;
				pixels.set(offset, red); pixels.set(offset + 1, green);
				pixels.set(offset + 2, blue); pixels.set(offset + 3, 255);
			}
		return Image.create(width, height, ImageFormat.RGBA8, pixels);
	}

	static function createOverlayImage(size:Int):Image {
		var pixels = Bytes.alloc(size * size * 4);
		var center = (size - 1) * 0.5;
		for (y in 0...size)
			for (x in 0...size) {
				var dx = x - center;
				var dy = y - center;
				var inside = dx * dx + dy * dy <= center * center;
				var offset = (y * size + x) * 4;
				pixels.set(offset, inside ? 132 : 0); pixels.set(offset + 1, inside ? 92 : 0);
				pixels.set(offset + 2, inside ? 242 : 0); pixels.set(offset + 3, inside ? 255 : 0);
			}
		return Image.create(size, size, ImageFormat.RGBA8, pixels);
	}

	static function createNineSliceImage(size:Int):Image {
		var pixels = Bytes.alloc(size * size * 4);
		for (y in 0...size)
			for (x in 0...size) {
				var edge = x < 10 || y < 10 || x >= size - 10 || y >= size - 10;
				var corner = (x < 10 || x >= size - 10) && (y < 10 || y >= size - 10);
				var offset = (y * size + x) * 4;
				pixels.set(offset, corner ? 39 : edge ? 54 : 225);
				pixels.set(offset + 1, corner ? 104 : edge ? 132 : 234);
				pixels.set(offset + 2, corner ? 205 : edge ? 224 : 248);
				pixels.set(offset + 3, 255);
			}
		return Image.create(size, size, ImageFormat.RGBA8, pixels);
	}

	static function loadDemoImage(path:Null<String>):Image {
		if (path != null && path.length > 0) {
			try {
				return Image.loadFile(path);
			} catch (_:Dynamic) {}
		}
		return createDemoImage(160, 96);
	}

	static function createPixelImage(filter:ImageFilter):Image {
		var size = 12;
		var pixels = Bytes.alloc(size * size * 4);
		for (y in 0...size)
			for (x in 0...size) {
				var bright = ((x >> 1) + (y >> 1)) % 2 == 0;
				var offset = (y * size + x) * 4;
				pixels.set(offset, bright ? 248 : 39);
				pixels.set(offset + 1, bright ? 179 : 105);
				pixels.set(offset + 2, bright ? 74 : 214);
				pixels.set(offset + 3, 255);
			}
		return Image.create(size, size, ImageFormat.RGBA8, pixels, filter);
	}

	public function getDiagnosticStage():Int
		return diagnosticStage;

	function applySmokeSelectState():Void {
		if (!state.smokeSelectManaged)
			return;
		if (state.smokeOpenSelect) {
			if (findExpandedCombo() == null) {
				var combo = findCombo();
				if (combo == null || !context.focusWidget(combo))
					throw "UI smoke test could not focus the Select control";
				context.key(UiEventKind.KeyDown, UiKey.Enter);
				var opened = buildRoot();
				context.submit(opened, frame);
				attachInspectorEvents();
			}
			if (findExpandedCombo() == null)
				throw "UI smoke test could not open the Select control";
		} else if (findExpandedCombo() != null) {
			context.key(UiEventKind.KeyDown, UiKey.Escape);
			var closed = buildRoot();
			context.submit(closed, frame);
			attachInspectorEvents();
		}
		state.smokeSelectManaged = false;
	}

	function findCombo():Null<WidgetId> {
		var result:Null<WidgetId> = null;
		if (context.root != null)
			context.root.walk(function(node) {
				var semantics:Semantics = cast node.semantics;
				if (result != null || semantics == null ||
					semantics.role != AccessibilityRole.ComboBox)
					return;
				result = node.id;
			});
		return result;
	}

	function findExpandedCombo():Null<WidgetId> {
		var result:Null<WidgetId> = null;
		if (context.root != null)
			context.root.walk(function(node) {
				if (result != null || node.semantics == null)
					return;
				var semantics:Semantics = cast node.semantics;
				if (semantics.role == AccessibilityRole.ComboBox &&
					(semantics.states & AccessibilityState.Expanded) != 0)
					result = node.id;
			});
		return result;
	}

	/** Returns the most recent submit/render metrics for the inspector and smoke runs. */
	public function latestFrameMetrics():Null<UiFrameMetrics>
		return context.frameMetrics;

	/** Prints a compact real-workload style/cache sample for profiling smoke runs. */
	public function printStats():Void {
		var metrics = latestFrameMetrics();
		if (metrics == null)
			return;
		Sys.println('nativekit_ui_showcase style_frame=${metrics.frameNumber}' +
			' nodes=${metrics.nodeCount}' +
			' style_resolutions=${metrics.styleResolutions}' +
			' style_changed=${metrics.styleChangedNodes}' +
			' style_unchanged=${metrics.styleUnchangedNodes}' +
			' invalidated=layout:${metrics.layoutInvalidatedNodes},text:${metrics.textLayoutInvalidatedNodes},' +
			'paint:${metrics.paintInvalidatedNodes},composite:${metrics.compositeInvalidatedNodes},' +
			'semantics:${metrics.semanticsInvalidatedNodes}' +
			' paint_work=${metrics.paintedNodes} rebuilt,${metrics.paintSkippedNodes} skipped,${metrics.emptyPaintNodes} empty' +
			' cache_hits=${metrics.styleCacheHits}' +
			' cache_misses=${metrics.styleCacheMisses}' +
			' cache_entries=${metrics.cachedStyleCount}' +
			' submit_mode=${metrics.reusedSubmission ? "reused" : "full"}' +
			' submit_ms=${milliseconds(metrics.submitSeconds)}' +
			' render_ms=${milliseconds(metrics.renderSeconds)}' +
			' total_ms=${milliseconds(metrics.totalSeconds)}');
	}

	static function milliseconds(seconds:Float):String
		return Std.string(Std.int(seconds * 1000.0 * 100.0) / 100.0);

	function buildRoot():Stack {
		var rootStyle = new LayoutStyle();
		rootStyle.width = LayoutAxis.grow();
		rootStyle.height = LayoutAxis.grow();
		var layers:Array<StackChild> = [new StackChild("explorer-shell", buildShell()),
			new StackChild("window-resize-zones", buildWindowResizeZones(), 0.0, 0.0, -1)];
		OverlayHost.appendLayers(this, layers);
		InspectionOverlay.addHighlight(this, layers);
		return new Stack("showcase-root", layers, rootStyle);
	}

	function buildWindowResizeZones():Column {
		var edge = WINDOW_RESIZE_EDGE;
		var rowStyle = new LayoutStyle();
		rowStyle.width = LayoutAxis.grow();
		rowStyle.height = LayoutAxis.fixed(edge);
		rowStyle.direction = LayoutDirection.LeftToRight;
		var middleStyle = new LayoutStyle();
		middleStyle.width = LayoutAxis.grow();
		middleStyle.height = LayoutAxis.grow();
		middleStyle.direction = LayoutDirection.LeftToRight;
		var columnStyle = new LayoutStyle();
		columnStyle.width = LayoutAxis.grow();
		columnStyle.height = LayoutAxis.grow();

		var north = new Row("window-chrome-north", [
			new KeyedView("northwest", resizeZone("northwest", WindowDecorationRegionKind.ResizeNorthwest,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge))),
			new KeyedView("north", resizeZone("north", WindowDecorationRegionKind.ResizeNorth,
				LayoutAxis.grow(), LayoutAxis.fixed(edge))),
			new KeyedView("northeast", resizeZone("northeast", WindowDecorationRegionKind.ResizeNortheast,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge)))
		], rowStyle);
		var middle = new Row("window-chrome-middle", [
			new KeyedView("west", resizeZone("west", WindowDecorationRegionKind.ResizeWest,
				LayoutAxis.fixed(edge), LayoutAxis.grow())),
			new KeyedView("center", new SizedBox("window-chrome-center", new Text(""),
				LayoutAxis.grow(), LayoutAxis.grow())),
			new KeyedView("east", resizeZone("east", WindowDecorationRegionKind.ResizeEast,
				LayoutAxis.fixed(edge), LayoutAxis.grow()))
		], middleStyle);
		var south = new Row("window-chrome-south", [
			new KeyedView("southwest", resizeZone("southwest", WindowDecorationRegionKind.ResizeSouthwest,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge))),
			new KeyedView("south", resizeZone("south", WindowDecorationRegionKind.ResizeSouth,
				LayoutAxis.grow(), LayoutAxis.fixed(edge))),
			new KeyedView("southeast", resizeZone("southeast", WindowDecorationRegionKind.ResizeSoutheast,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge)))
		], rowStyle);
		return new Column("window-chrome-zones", [
			new KeyedView("north", north),
			new KeyedView("middle", middle),
			new KeyedView("south", south)
		], columnStyle);
	}

	function resizeZone(key:String, kind:WindowDecorationRegionKind,
			width:LayoutAxis, height:LayoutAxis):View
		return new WindowChrome(key, kind, new SizedBox(key + "-box", new Text(""), width, height));

	function buildShell():View {
		return ExplorerShell.build(this);
	}

	function buildPage():View {
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
		var pageContent = new Column("page-content-" + (page == null ? "overview" : page.id), items, style);
		return new DefaultTextStyle(pageContent,
			TextStyleOverride.paragraph(TextWrap.WordCharacter));
	}

	function pageHeading(items:Array<KeyedView>, title:String, description:String):Void {
		PageHeader.append(items, title, description);
	}

	function buildInspector():Column {
		return InspectorPanel.build(this);
	}

	function attachInspectorEvents():Void {
		InspectionOverlay.attachEvents(this);
	}

	function textField():TextField {
		return ShowcaseKit.textField(this);
	}

	function textArea():TextArea
		return ShowcaseKit.textArea(this);

	/** Focuses a text editor by its semantic label for showcase test controls. */
	function focusTextEditor(label:String):Bool {
		for (record in context.inspect())
			if (record.visible && record.enabled && record.focusable && record.label == label)
				return context.focusWidget(new WidgetId(record.id));
		return false;
	}

	/** Sends a platform-equivalent editing shortcut to a named showcase editor. */
	function textCommand(label:String, key:Int):Bool {
		if (!focusTextEditor(label))
			return false;
		var modifiers = #if (mac || ios)
			UiModifier.Super;
		#else
			UiModifier.Control;
		#end
		context.key(UiEventKind.KeyDown, key, modifiers);
		return true;
	}

	function recordTextDiagnostics(value:TextEditorDiagnostics):Void {
		if (value == null)
			return;
		if (value.focused)
			state.textDiagnostics = value;
		else if (state.textDiagnostics != null && state.textDiagnostics.key == value.key)
			state.textDiagnostics = null;
	}

	function recordTextClipboardAction(action:String):Void
		state.textLastClipboardAction = action;

	function recordTextSubmit(label:String):Void
		state.textLastSubmit = label;

	function slider():Slider
		return ShowcaseKit.slider(this);

	function button(label:String, key:String, action:Void->Void, selected:Bool = false):Button
		return ShowcaseKit.button(this, label, key, action, selected);

	function disabledButton(label:String):Button
		return ShowcaseKit.disabledButton(this, label);

	function panel(key:String, children:Array<KeyedView>):Column
		return ShowcaseKit.panel(this, key, children);

	function panelStyle(?fixedWidth:Float):LayoutStyle
		return ShowcaseKit.panelStyle(this, fixedWidth);

	function columnStyle(width:Float, height:Float):LayoutStyle
		return ShowcaseKit.columnStyle(this, width, height);

	function rowStyle(gap:Float):LayoutStyle
		return ShowcaseKit.rowStyle(gap);

	function fixedBoxStyle(width:Float, height:Float):LayoutStyle
		return ShowcaseKit.fixedBoxStyle(this, width, height);

	function colorTile(label:String, background:Color, growWeight:Float = 1.0):View
		return ShowcaseKit.colorTile(label, background, growWeight);

	function stackDemo():View
		return ShowcaseKit.stackDemo(this);

	function keyed(key:String, view:View):KeyedView
		return ShowcaseKit.keyed(key, view);

	function text(value:String, ?color:Color):Text
		return ShowcaseKit.text(value, color);

	function heading(value:String):Text
		return ShowcaseKit.heading(value);

	function caption(value:String):Text
		return ShowcaseKit.caption(value);

	function label(value:String):Text
		return ShowcaseKit.label(value);

	function paletteBackground():Color
		return ShowcaseKit.paletteBackground(this);

	function paletteSidebar():Color
		return ShowcaseKit.paletteSidebar(this);

	function paletteText():Color
		return ShowcaseKit.paletteText(this);

	function paletteMuted():Color
		return ShowcaseKit.paletteMuted(this);

	static function makeTheme(light:Bool):Theme
		return ShowcaseKit.makeTheme(light);

	static inline function color(red:Float, green:Float, blue:Float, alpha:Float = 1.0):Color
		return ShowcaseKit.color(red, green, blue, alpha);

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
