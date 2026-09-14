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
import pages.ControlsPage;
import pages.GraphicsPage;
import pages.GesturesPage;
import pages.LayoutPage;
import pages.ListsPage;
import pages.OverlaysPage;
import pages.OverviewPage;
import pages.TextPage;
import shell.CatalogSidebar;
import shell.ExplorerShell;

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
	var inspectorTab:String = "preview";
	var selectedNodeId:Int = 0;
	var hoveredNodeId:Int = 0;
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
	var tapCount:Int = 0;
	var doubleTapCount:Int = 0;
	var longPressCount:Int = 0;
	var dragCount:Int = 0;
	var dragCardX:Float = 18.0;
	var dragCardY:Float = 18.0;
	var dragOriginX:Float = 18.0;
	var dragOriginY:Float = 18.0;
	var gestureMessage:String = "Tap, double-tap, hold, or drag the card.";
	var smokeFocusTextField:Bool = false;
	var visualFocusLabel:Null<String>;
	var visualTextAreaSelection:Bool = false;

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
				rowStyle.background = lightTheme
					? (index % 2 == 0 ? color(0.98, 0.99, 1.0) : color(0.91, 0.94, 0.98))
					: (index % 2 == 0 ? color(0.11, 0.14, 0.20) : color(0.13, 0.16, 0.23));
				return new Text('ROW ${index + 1}  ·  virtual item', rowStyle, paletteText());
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
		switch frame % 12 {
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
			case 10: selectedPage = "graphics";
			case 11: selectedPage = "gestures";
			default: selectedPage = "overview";
		}
	}

	/** Selects one of the deterministic browser screenshot states, 0-14. */
	public function setVisualCase(caseId:Int):Bool {
		showDialog = false;
		showPopup = false;
		showMenu = false;
		searchText = "";
		inspectorOpen = true;
		inspectorTab = "preview";
		selectedNodeId = 0;
		hoveredNodeId = 0;
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
		visualTextAreaSelection = false;
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
			case 11: selectedPage = "gestures";
			case 12:
				selectedPage = "lists";
				lightTheme = true;
				context.setTheme(makeTheme(true));
			case 13:
				selectedPage = "text";
				visualFocusLabel = "Multilingual notes";
				visualTextAreaSelection = true;
			case 14:
				selectedPage = "overlays";
				lightTheme = true;
				context.setTheme(makeTheme(true));
				showMenu = true;
			default: return false;
		}
		return true;
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
		if (smokeFocusTextField || visualFocusLabel != null) {
			diagnosticStage = 4;
			smokeFocusTextField = false;
			var targetLabel = visualFocusLabel;
			visualFocusLabel = null;
			var selectTextArea = visualTextAreaSelection;
			visualTextAreaSelection = false;
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
		var highlightId = hoveredNodeId != 0 ? hoveredNodeId : selectedNodeId;
		if (inspectorOpen && highlightId != 0) {
			var overlayStyle = new LayoutStyle();
			overlayStyle.width = LayoutAxis.grow();
			overlayStyle.height = LayoutAxis.grow();
			var highlight = new CanvasView("inspector-highlight", function(canvas, _) {
				drawInspectionHighlight(canvas, findSnapshot(context.inspect(), highlightId));
			}, overlayStyle, null, false);
			layers.push(new StackChild("inspector-highlight-layer", highlight, 0.0, 0.0, 32767));
		}
		return new Stack("showcase-root", layers, rootStyle);
	}

	function buildShell():Column {
		return ExplorerShell.build(this);
	}

	function buildCatalog():Column {
		return CatalogSidebar.build(this);
	}

	function buildPage():Column {
		var items:Array<KeyedView> = [];
		switch selectedPage {
			case "controls": buildControls(items);
			case "text": buildTextPage(items);
			case "layout": buildLayoutPage(items);
			case "lists": buildListsPage(items);
			case "overlays": buildOverlaysPage(items);
			case "gestures": buildGesturesPage(items);
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
		OverviewPage.build(this, items);
	}

	function buildControls(items:Array<KeyedView>):Void {
		ControlsPage.build(this, items);
	}

	function buildTextPage(items:Array<KeyedView>):Void {
		TextPage.build(this, items);
	}

	function buildLayoutPage(items:Array<KeyedView>):Void {
		LayoutPage.build(this, items);
	}

	function buildListsPage(items:Array<KeyedView>):Void {
		ListsPage.build(this, items);
	}

	function buildOverlaysPage(items:Array<KeyedView>):Void {
		OverlaysPage.build(this, items);
	}

	function buildGesturesPage(items:Array<KeyedView>):Void {
		GesturesPage.build(this, items);
	}

	function buildGraphicsPage(items:Array<KeyedView>):Void {
		GraphicsPage.build(this, items);
	}

	function buildInspector():Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(270.0);
		style.height = LayoutAxis.grow();
		style.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		style.childGap = 8.0;
		style.background = paletteSidebar();
		var records:Array<UiNodeSnapshot> = context.inspect();
		var issues:Array<AccessibilityIssue> = context.auditAccessibility();
		var selected = chooseInspectionRecord(records);
		var children:Array<KeyedView> = [
			keyed("title", text("INSPECT", paletteText())),
			keyed("subtitle", text("Hover to preview · click to pin", paletteMuted())),
			keyed("tab-row-one", new Row("inspector-tab-row-one", [
				keyed("preview", inspectorTabButton("Preview", "preview")),
				keyed("state", inspectorTabButton("State", "state"))
			], rowStyle(6.0))),
			keyed("tab-row-two", new Row("inspector-tab-row-two", [
				keyed("semantics", inspectorTabButton("Semantics", "semantics")),
				keyed("tree", inspectorTabButton("Tree", "tree"))
			], rowStyle(6.0)))
		];
		var contentStyle = new LayoutStyle();
		contentStyle.width = LayoutAxis.grow();
		contentStyle.height = LayoutAxis.grow();
		contentStyle.clipVertical = true;
		var body = new ScrollView("inspector-content-scroll",
			buildInspectorContent(selected, records, issues), contentStyle, ScrollAxis.Vertical);
		children.push(keyed("content", body));
		var auditColor = issues.length == 0 ? color(0.35, 0.85, 0.69) : color(0.96, 0.58, 0.31);
		children.push(keyed("audit-summary", text(issues.length == 0
			? "Accessibility audit · clean" : 'Accessibility audit · ${issues.length} issue(s)', auditColor)));
		return new Column("inspector-drawer", children, style);
	}

	function inspectorTabButton(label:String, tab:String):Button {
		var item = button(label, "inspector-tab-" + tab, function() { inspectorTab = tab; }, inspectorTab == tab);
		item.style.width = LayoutAxis.grow();
		item.style.height = LayoutAxis.fixed(30.0);
		item.style.padding = new Insets(7.0, 5.0, 7.0, 5.0);
		return item;
	}

	function buildInspectorContent(record:Null<UiNodeSnapshot>, records:Array<UiNodeSnapshot>,
			issues:Array<AccessibilityIssue>):Column {
		var children:Array<KeyedView> = [];
		if (record == null) {
			children.push(keyed("empty", text("Move over a widget in the preview to inspect it. Click to keep it selected.", paletteMuted())));
			return new Column("inspector-empty-content", children);
		}
		var label = record.label == null || record.label.length == 0 ? "Unnamed node" : record.label;
		children.push(keyed("selected-label", text(label, paletteText())));
		children.push(keyed("selected-id", text('#${record.id} · ${visualName(record.visualKind)} · ${roleName(record.role)}', paletteMuted())));
		switch inspectorTab {
			case "state":
				children.push(keyed("state-heading", text("LIVE STATE", paletteText())));
				children.push(keyed("state-focus", text('focused=${record.focused}  focusable=${record.focusable}', paletteMuted())));
				children.push(keyed("state-pointer", text('hovered=${record.hovered}  pressed=${record.pressed}', paletteMuted())));
				children.push(keyed("state-enabled", text('enabled=${record.enabled}  visible=${record.visible}', paletteMuted())));
				children.push(keyed("state-value", text('value=${record.value == null ? "(none)" : record.value}', paletteMuted())));
				children.push(keyed("state-semantic", text('semantic states: ${semanticStateNames(record.semanticStates)}', paletteMuted())));
				children.push(keyed("state-bounds", text('bounds: ${rectText(record.bounds)}\nclip: ${rectText(record.clipBounds)}\nz-order: ${record.zIndex}', paletteMuted())));
			case "semantics":
				children.push(keyed("sem-heading", text("ACCESSIBILITY", paletteText())));
				children.push(keyed("sem-role", text('role: ${roleName(record.role)}', paletteMuted())));
				children.push(keyed("sem-label", text('label: ${label}', paletteMuted())));
				children.push(keyed("sem-value", text('value: ${record.value == null ? "(none)" : record.value}', paletteMuted())));
				children.push(keyed("sem-states", text('states: ${semanticStateNames(record.semanticStates)}', paletteMuted())));
				children.push(keyed("sem-actions", text('actions: ${actionNames(record.actions)}', paletteMuted())));
				var nodeIssues:Array<String> = [];
				for (issue in issues)
					if (issue.nodeId == record.id)
						nodeIssues.push(issue.code + ": " + issue.message);
				children.push(keyed("sem-audit-heading", text("AUDIT FINDINGS", paletteText())));
				children.push(keyed("sem-audit", text(nodeIssues.length == 0
					? "No accessibility issues for this node." : nodeIssues.join("\n"),
					nodeIssues.length == 0 ? color(0.35, 0.85, 0.69) : color(0.96, 0.58, 0.31))));
			case "tree":
				children.push(keyed("tree-heading", text("ANCESTRY", paletteText())));
				for (line in treeAncestry(record, records))
					children.push(keyed("ancestor-" + children.length, text(line, paletteMuted())));
				children.push(keyed("children-heading", text("CHILD NODES", paletteText())));
				var shown = 0;
				for (child in records)
					if (child.parentId == record.id && shown < 8) {
						children.push(keyed('child-${shown}', text(treeNodeText(child), paletteMuted())));
						shown++;
					}
				if (shown == 0)
					children.push(keyed("tree-leaf", text("No child render nodes.", paletteMuted())));
			case _:
				var synopsis = widgetSynopsis(record);
				children.push(keyed("preview-heading", text("WHAT THIS IS", paletteText())));
				children.push(keyed("preview-description", text(synopsis.description, paletteMuted())));
				children.push(keyed("behavior-heading", text("BEHAVIOR", paletteText())));
				children.push(keyed("behavior-description", text(synopsis.behavior, paletteMuted())));
				children.push(keyed("code-heading", text("HAXE", paletteText())));
				children.push(keyed("code-snippet", text(synopsis.code, color(0.48, 0.82, 0.75))));
				children.push(keyed("geometry-heading", text("RESOLVED GEOMETRY", paletteText())));
				children.push(keyed("geometry", text('bounds ${rectText(record.bounds)}\nclip ${rectText(record.clipBounds)}\nz ${record.zIndex}', paletteMuted())));
		}
		return new Column("inspector-content-" + inspectorTab, children, panelStyle());
	}

	function chooseInspectionRecord(records:Array<UiNodeSnapshot>):Null<UiNodeSnapshot> {
		if (hoveredNodeId != 0) {
			var hovered = findSnapshot(records, hoveredNodeId);
			if (hovered != null)
				return hovered;
		}
		if (selectedNodeId != 0) {
			var selected = findSnapshot(records, selectedNodeId);
			if (selected != null)
				return selected;
		}
		for (record in records)
			if (record.focused && isRecordInPreview(record))
				return record;
		for (record in records)
			if (record.visible && record.focusable && record.role >= 0 && isRecordInPreview(record))
				return record;
		return null;
	}

	function attachInspectorEvents():Void {
		if (context.root == null)
			return;
		var root = context.root;
		root.on(UiEventKind.PointerMove, function(event) {
			updateHoveredAt(event.x, event.y);
		});
		root.on(UiEventKind.HoverEnter, function(event) {
			updateHoveredAt(event.x, event.y);
		});
		root.on(UiEventKind.HoverLeave, function(event) {
			updateHoveredAt(event.x, event.y);
		});
		root.on(UiEventKind.Focus, function(event) {
			var focusedId = inspectionTargetId(event.target);
			var focused = findSnapshot(context.inspect(), focusedId);
			if (focused != null && isRecordInPreview(focused))
				selectedNodeId = focusedId;
		});
		root.on(UiEventKind.PointerDown, function(event) {
			if (isPreviewPoint(event.x, event.y)) {
				var selected = inspectionTargetId(event.target);
				if (selected != 0) {
					selectedNodeId = selected;
					hoveredNodeId = selected;
				}
			}
		});
	}

	function updateHoveredAt(x:Float, y:Float):Void {
		if (!isPreviewPoint(x, y) || context.root == null) {
			hoveredNodeId = 0;
			return;
		}
		var path = HitTest.path(context.root, x, y);
		hoveredNodeId = path.length == 0 ? 0 : inspectionPathTargetId(path);
	}

	function inspectionTargetId(id:WidgetId):Int {
		if (context.root == null)
			return 0;
		return inspectionPathTargetId(HitTest.pathTo(context.root.find(id)));
	}

	function inspectionPathTargetId(path:Array<RenderNode>):Int {
		if (path.length == 0)
			return 0;
		var id = path[path.length - 1].id;
		var semantic:Null<WidgetId> = null;
		var index = path.length - 1;
		while (index >= 0) {
			var node = path[index];
			if (node.semantics != null && semantic == null)
				semantic = node.id;
			if (node.focusable)
				return node.id.value;
			index--;
		}
		return semantic == null ? id.value : semantic.value;
	}

	function isPreviewPoint(x:Float, y:Float):Bool {
		var sidebar = width < 760.0 ? 176.0 : 212.0;
		var inspectorWidth = inspectorOpen && width >= 880.0 ? 270.0 : 0.0;
		return y >= 66.0 && x >= sidebar && x < width - inspectorWidth;
	}

	function isRecordInPreview(record:UiNodeSnapshot):Bool {
		var centerX = record.bounds.x + record.bounds.width * 0.5;
		var centerY = record.bounds.y + record.bounds.height * 0.5;
		return isPreviewPoint(centerX, centerY);
	}

	function drawInspectionHighlight(canvas:Canvas, record:Null<UiNodeSnapshot>):Void {
		if (record == null || record.bounds.width <= 0.0 || record.bounds.height <= 0.0)
			return;
		var left = Math.max(record.bounds.x, record.clipBounds.x);
		var top = Math.max(record.bounds.y, record.clipBounds.y);
		var right = Math.min(record.bounds.x + record.bounds.width,
			record.clipBounds.x + record.clipBounds.width);
		var bottom = Math.min(record.bounds.y + record.bounds.height,
			record.clipBounds.y + record.clipBounds.height);
		if (right <= left || bottom <= top)
			return;
		var edge = Math.min(2.0, Math.min((right - left) * 0.5, (bottom - top) * 0.5));
		var outline = color(0.29, 0.92, 0.72, 0.95);
		canvas.fillRect(new Rect(left, top, right - left, edge), outline);
		canvas.fillRect(new Rect(left, bottom - edge, right - left, edge), outline);
		canvas.fillRect(new Rect(left, top + edge, edge, Math.max(0.0, bottom - top - 2.0 * edge)), outline);
		canvas.fillRect(new Rect(right - edge, top + edge, edge, Math.max(0.0, bottom - top - 2.0 * edge)), outline);
	}

	function treeAncestry(record:UiNodeSnapshot, records:Array<UiNodeSnapshot>):Array<String> {
		var chain:Array<UiNodeSnapshot> = [record];
		var parentId = record.parentId;
		while (parentId != 0 && chain.length < 32) {
			var parent = findSnapshot(records, parentId);
			if (parent == null)
				break;
			chain.push(parent);
			parentId = parent.parentId;
		}
		chain.reverse();
		var result:Array<String> = [];
		for (index in 0...chain.length) {
			var indent = "";
			for (_ in 0...index)
				indent += "  ";
			result.push(indent + treeNodeText(chain[index]));
		}
		return result;
	}

	function treeNodeText(record:UiNodeSnapshot):String {
		var label = record.label == null || record.label.length == 0 ? "" : ' · ${record.label}';
		return '#${record.id} ${visualName(record.visualKind)}${label} · ${roleName(record.role)} · z=${record.zIndex}';
	}

	function widgetSynopsis(record:UiNodeSnapshot):{description:String, behavior:String, code:String} {
		return switch record.role {
			case 1: {
				description: "A compositional action control. Haxe owns its interaction state and semantic action; the render tree contains a box and label.",
				behavior: "Activate with click, Enter or Space. It participates in keyboard focus and exposes an Activate accessibility action.",
				code: 'new Button("Save changes", style, onActivate, "save")'
			};
			case 2: {
				description: "A boolean selection control composed from ordinary Haxe layout and paint nodes.",
				behavior: "Click or press Space to toggle. Checked state is reflected in the semantic value and state bits.",
				code: 'new Checkbox("show-labels", "Show labels", checked, onChange)'
			};
			case 3: {
				description: "One option in an exclusive radio selection group.",
				behavior: "Arrow keys move between enabled options; Space or Enter selects the focused option.",
				code: 'new RadioGroup("density", options, selected, onChange)'
			};
			case 5: {
				description: "An editable text control backed by framework editor state and NativeKit text-input services.",
				behavior: "Pointer hit testing positions the caret; keyboard selection/editing and platform clipboard and IME are supported where available.",
				code: 'new TextField("name", value, onChange, style, "Display name")'
			};
			case 11: {
				description: "A continuous range control with pointer, keyboard and semantic value support.",
				behavior: "Drag the thumb or track; arrow keys and accessibility increment/decrement adjust the snapped value.",
				code: 'new Slider("volume", "Volume", value, 0, 1, 0.01, onChange)'
			};
			case 12: {
				description: "A clipped viewport that translates persistent content using Haxe-owned scroll state.",
				behavior: "Wheel, touch/drag, and semantic forward/back actions update the scroll controller.",
				code: 'new ScrollView("results", content, style, ScrollAxis.Vertical)'
			};
			case 9, 10: {
				description: "A list container or a realized item within a data-oriented view.",
				behavior: "The virtual list builds only visible fixed-height rows plus a small overscan window.",
				code: 'new VirtualList("rows", count, rowHeight, buildRow, style)'
			};
			case 7: {
				description: "A rendered image with an optional accessible name.",
				behavior: "The image participates in normal layout, clipping and render order.",
				code: 'new ImageView("avatar", image, "Profile photo")'
			};
			case 8: {
				description: "A heading that identifies a section of the current demo.",
				behavior: "Headings expose a navigable semantic role while rendering with the shared text pipeline.",
				code: 'new Text("Section title", headingStyle)'
			};
			case 0: {
				description: "A layout or grouping node in the Haxe-owned render tree.",
				behavior: "It composes children and contributes resolved bounds, clipping and z-order without adding a native widget abstraction.",
				code: 'new Column("settings", [keyed("name", nameField)])'
			};
			default: {
				description: "A render primitive produced by a Haxe widget or layout composition.",
				behavior: "Geometry and clipping are resolved natively; events and semantic identity remain connected to this Haxe node.",
				code: 'new Text("Hello, NativeKit UI")'
			};
		};
	}

	function semanticStateNames(states:Int):String {
		var values:Array<String> = [];
		if ((states & AccessibilityState.Focusable) != 0) values.push("Focusable");
		if ((states & AccessibilityState.Focused) != 0) values.push("Focused");
		if ((states & AccessibilityState.Selected) != 0) values.push("Selected");
		if ((states & AccessibilityState.Checked) != 0) values.push("Checked");
		if ((states & AccessibilityState.Disabled) != 0) values.push("Disabled");
		if ((states & AccessibilityState.ReadOnly) != 0) values.push("ReadOnly");
		if ((states & AccessibilityState.Multiline) != 0) values.push("Multiline");
		if ((states & AccessibilityState.Password) != 0) values.push("Password");
		if ((states & AccessibilityState.Expanded) != 0) values.push("Expanded");
		return values.length == 0 ? "(none)" : values.join(", ");
	}

	function actionNames(actions:Int):String {
		var values:Array<String> = [];
		if ((actions & AccessibilityAction.Activate) != 0) values.push("Activate");
		if ((actions & AccessibilityAction.Focus) != 0) values.push("Focus");
		if ((actions & AccessibilityAction.SetValue) != 0) values.push("SetValue");
		if ((actions & AccessibilityAction.SetSelection) != 0) values.push("SetSelection");
		if ((actions & AccessibilityAction.Increment) != 0) values.push("Increment");
		if ((actions & AccessibilityAction.Decrement) != 0) values.push("Decrement");
		if ((actions & AccessibilityAction.ScrollForward) != 0) values.push("ScrollForward");
		if ((actions & AccessibilityAction.ScrollBackward) != 0) values.push("ScrollBackward");
		if ((actions & AccessibilityAction.MoveNext) != 0) values.push("MoveNext");
		if ((actions & AccessibilityAction.MovePrevious) != 0) values.push("MovePrevious");
		return values.length == 0 ? "(none)" : values.join(", ");
	}

	function rectText(rect:Rect):String
		return '${Std.int(rect.x)},${Std.int(rect.y)} ${Std.int(rect.width)}×${Std.int(rect.height)}';

	function visualName(kind:Int):String {
		return switch kind {
			case 1: "Box";
			case 2: "Text";
			case 3: "Image";
			case 4: "Custom";
			default: "Unknown";
		};
	}

	function findSnapshot(records:Array<UiNodeSnapshot>, id:Int):Null<UiNodeSnapshot> {
		if (id == 0)
			return null;
		for (record in records)
			if (record.id == id)
				return record;
		return null;
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
