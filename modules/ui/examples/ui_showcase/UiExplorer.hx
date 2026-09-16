import Color;
import FontCollection;
import FrameInfo;
import LayoutAxis;
import LayoutFrame;
import LayoutStyle;
import NativeKit.Handle;
import NativeKit.Capabilities;
import NativeKit.SurfaceHandle;
import NativeKitEvents;
import NativeKitSurface;
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
import nativekit.ui.core.TextStyleOverride;
import TextWrap;
import nativekit.ui.widgets.VirtualList;
import ExplorerCatalog;
import shell.ExplorerShell;
import shell.OverlayHost;
import inspector.InspectionOverlay;
import inspector.InspectorPanel;
import testing.ExplorerSmokeSequence;
import testing.ExplorerFocusSequence;
import testing.ExplorerVisualCases;
import components.PageHeader;
import components.ShowcaseKit;
import pages.ListsPage;

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
@:allow(testing.ExplorerFocusSequence)
@:allow(components.ShowcaseKit)
@:allow(shell.OverlayHost)
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
	final staticSubmitReuse:Bool;
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
			onOpenGraphics:Void->Void, ?staticSubmitReuse:Bool) {
		if (fonts == null || fonts.isDisposed())
			throw "UI Explorer requires a live font collection";
		this.fonts = fonts;
		state = new ExplorerState();
		this.platformLabel = platformLabel == null ? "NativeKit runtime" : platformLabel;
		this.onOpenGraphics = onOpenGraphics == null ? function() {} : onOpenGraphics;
		this.staticSubmitReuse = staticSubmitReuse == true;
		context = new UiContext(null, fonts, makeTheme(false));
		renderer = Renderer.create();
		width = 900.0;
		height = 650.0;
		framebufferWidth = 900;
		framebufferHeight = 650;
		pixelScale = 1.0;
		frame = new LayoutFrame(width, height);
		frameInfo = new FrameInfo(width, height, framebufferWidth, framebufferHeight, pixelScale);
		virtualList = ListsPage.createVirtualList(this);
		tweenController = new AnimationController(context.animations, function(value) {
			state.gestures.tweenValue = value;
		});
		springController = new SpringController(state.gestures.springValue, 180.0, 24.0, 1.0, 0.001,
			context.animations, function(value) { state.gestures.springValue = value; });
	}

	/** Installs the native surface used by text editing, IME state, and accessibility. */
	public function attachSurface(surface:NativeKitSurface):Void {
		nativeSurface = surface;
		context.attachPlatformSurface(surface);
		if (supportsNativeAccessibility())
			context.updateAccessibility(surface);
	}

	/** Routes platform input through the framework's standard NativeKit adapter. */
	public function attachInput(events:NativeKitEvents, window:Handle):NativeInputAdapter {
		var accessibilitySource = nativeSurface == null || !supportsNativeAccessibility() ? window :
			new Handle(nativeSurface.nativeHandle().rawValue());
		var input = new NativeInputAdapter(context, window, accessibilitySource);
		input.attach(events);
		return input;
	}

	function supportsNativeAccessibility():Bool
		return haxe.Int64.compare(
			haxe.Int64.and(NativeKit.nk_get_capabilities(), Capabilities.accessibility()),
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
			' paint_work=${metrics.paintedNodes} rebuilt,${metrics.paintSkippedNodes} skipped' +
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
		var layers:Array<StackChild> = [new StackChild("explorer-shell", buildShell())];
		OverlayHost.appendLayers(this, layers);
		InspectionOverlay.addHighlight(this, layers);
		return new Stack("showcase-root", layers, rootStyle);
	}

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
