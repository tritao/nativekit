import FontCollection;
import FrameInfo;
import Insets;
import LayoutAxis;
import LayoutAlignmentY;
import LayoutDirection;
import LayoutDistribution;
import LayoutFrame;
import LayoutStyle;
import NativeKit;
import NativeKit.GraphicsApi;
import NativeKitGpu;
import NativeKit.Handle;
import NativeKit.WindowFlags;
import NativeKit.WindowHandle;
import NativeKit.WindowKind;
import NativeKit.WindowOptions;
import NativeKit.WindowSizeLimits;
import NativeKit.SurfaceFlags;
import NativeKit.SurfaceHandle;
import NativeKit.SurfaceOptions;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitSurface;
import NativeKitError;
import Renderer;
import Surface;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.View;
import nativekit.ui.theme.Theme;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.SizedBox;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.WindowChrome;
import NativeKit.WindowDecorationRegionKind;

/** Owns the second native window used by the custom window-chrome showcase. */
class WindowChromeDemo {
	var window:WindowHandle;
	var surface:SurfaceHandle;
	var nativeSurface:Null<NativeKitSurface>;
	var fonts:Null<FontCollection>;
	var context:Null<UiContext>;
	var renderer:Null<Renderer>;
	var input:Null<NativeInputAdapter>;
	var frame:Null<LayoutFrame>;
	var frameInfo:Null<FrameInfo>;
	var frameSubscription:Null<NativeKitSurfaceFrameSubscription>;
	var width:Int = 640;
	var height:Int = 420;
	var scale:Float = 1.0;
	var ready:Bool = false;
	var closeRequested:Bool = false;
	var disposed:Bool = false;
	var clientActionPressed:Bool = false;
	var failure:Null<Dynamic>;
	var renderedFrames:Int = 0;
	var closeAfterFrames:Int = 0;
	static inline var WINDOW_POSITION_OFFSET:Int = 40;
	static inline var MIN_WIDTH:Int = 480;
	static inline var MIN_HEIGHT:Int = 300;

	public static function open(events:NativeKitEvents, graphicsApi:GraphicsApi,
			owner:WindowHandle, theme:Theme):WindowChromeDemo {
		var demo = new WindowChromeDemo();
		try {
			demo.initialize(events, graphicsApi, owner, theme);
			return demo;
		} catch (error:Dynamic) {
			demo.dispose();
			throw error;
		}
	}

	private function new() {
		window = WindowHandle.invalid();
		surface = SurfaceHandle.invalid();
	}

	function initialize(events:NativeKitEvents, graphicsApi:GraphicsApi,
		owner:WindowHandle, theme:Theme):Void {
		if (events == null || owner == null || !owner.isValid())
			throw "Window chrome demo requires a live event pump and owner window";

		var windowOptions = new WindowOptions();
		windowOptions.set_width(width);
		windowOptions.set_height(height);
		windowOptions.set_title("NativeKit Window Chrome Demo");
		windowOptions.set_flags(WindowFlags.Resizable | WindowFlags.Borderless);
		windowOptions.set_owner(owner);
		windowOptions.set_kind(WindowKind.Utility);
		var createdWindow = NativeKit.nk_window_create(windowOptions);
		if (createdWindow.status != Result.Ok)
			throw new NativeKitError(createdWindow.status, "window.create", NativeKit.nk_last_error());
		window = createdWindow.out_window.borrow();
		applySizeLimits();
		placeNear(owner);

		var surfaceOptions = new SurfaceOptions();
		var surfaceFlags = SurfaceFlags.Stencil;
		if (graphicsApi == GraphicsApi.Opengl) {
			surfaceFlags = SurfaceFlags.ForwardCompatible | SurfaceFlags.Stencil;
			surfaceOptions.set_major_version(3);
			surfaceOptions.set_minor_version(3);
		}
		surfaceOptions.set_flags(surfaceFlags);
		surfaceOptions.set_api(graphicsApi);
		surfaceOptions.set_width(width);
		surfaceOptions.set_height(height);
		var createdSurface = NativeKit.nk_surface_create(new Handle(window.rawValue()), surfaceOptions);
		if (createdSurface.status != Result.Ok)
			throw new NativeKitError(createdSurface.status, "surface.create", NativeKit.nk_last_error());
		surface = createdSurface.out_surface.borrow();
		nativeSurface = NativeKitSurface.borrowNativeHandle(surface);

		fonts = FontCollection.create();
		fonts.addSystemFallbacks();
		context = new UiContext(null, fonts, theme == null ? new Theme() : theme);
		context.attachPlatformSurface(nativeSurface);
		context.attachPlatformWindow(window);
		renderer = Renderer.create();
		frame = new LayoutFrame(width, height);
		frameInfo = new FrameInfo(width, height, width, height, scale);
		input = new NativeInputAdapter(context, new Handle(window.rawValue()));
		input.attach(events);
	}

	/** Handles window and surface lifecycle events; input is routed by NativeInputAdapter. */
	public function handleEvent(value:NativeKitEventValue):Void {
		if (disposed || value == null)
			return;
		switch (value) {
			case WindowClose(source) if (source.rawValue() == window.rawValue()):
				closeRequested = true;
			case WindowResize(source, newWidth, newHeight)
				if (source.rawValue() == window.rawValue()):
				width = newWidth;
				height = newHeight;
				if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) != Result.Ok) {
					failure = "surface.resize failed: " + NativeKit.nk_last_error();
					closeRequested = true;
				}
			case WindowScaleChanged(source, newScale) if (source.rawValue() == window.rawValue()):
				scale = newScale > 0.0 ? newScale : 1.0;
			case SurfaceReady(source) if (source.rawValue() == surface.rawValue()):
				ready = true;
				var size = NativeKit.nk_surface_get_framebuffer_size(surface);
				if (size.status != Result.Ok) {
					failure = new NativeKitError(size.status, "surface.framebuffer_size", NativeKit.nk_last_error());
					closeRequested = true;
					return;
				}
				frameInfo = new FrameInfo(width, height, size.out_width, size.out_height, scale);
				if (frameSubscription == null)
					frameSubscription = nativeSurface.onFrame(function(framebufferWidth, framebufferHeight) {
						try
							renderFrame(framebufferWidth, framebufferHeight);
						catch (error:Dynamic) {
							failure = error;
							closeRequested = true;
						}
					});
			case SurfaceResize(source, newWidth, newHeight, framebufferWidth, framebufferHeight)
				if (source.rawValue() == surface.rawValue()):
				width = newWidth;
				height = newHeight;
				frameInfo = new FrameInfo(width, height, framebufferWidth, framebufferHeight, scale);
			case SurfaceLost(source) if (source.rawValue() == surface.rawValue()):
				ready = false;
			case _:
		}
	}

	public function isCloseRequested():Bool
		return closeRequested;

	public function renderedFrameCount():Int
		return renderedFrames;

	public function enableDiagnosticRun(frameCount:Int):Void
		closeAfterFrames = frameCount > 0 ? frameCount : 1;

	public function minimumWidth():Int
		return MIN_WIDTH;

	public function minimumHeight():Int
		return MIN_HEIGHT;

	/** Replaces the demo palette when the owning explorer changes appearance. */
	public function setTheme(theme:Theme):Void {
		if (disposed || context == null || theme == null)
			return;
		context.setTheme(theme);
	}

	public function failureMessage():Null<String> {
		if (failure == null)
			return null;
		if (Std.isOfType(failure, UiError)) {
			var uiError:UiError = cast failure;
			var stage = context == null ? 0 : context.getDiagnosticStage();
			return 'UI ${uiError.operation} failed with status ${uiError.status} (stage ${stage}): '
				+ NativeKitGpu.nkgpu_last_error();
		}
		return Std.string(failure);
	}

	function renderFrame(framebufferWidth:Int, framebufferHeight:Int):Void {
		if (!ready || disposed || context == null || renderer == null || frame == null ||
			frameInfo == null || nativeSurface == null || framebufferWidth <= 0 || framebufferHeight <= 0)
			return;
		frame.setViewport(width, height);
		frame.deltaSeconds = 1.0 / 60.0;
		frameInfo.set(width, height, framebufferWidth, framebufferHeight, scale);
		context.submit(buildRoot(), frame);
		context.render(renderer, Surface.fromNativeHandle(surface), frameInfo);
		renderedFrames++;
		if (closeAfterFrames > 0 && renderedFrames >= closeAfterFrames)
			closeRequested = true;
	}

	function placeNear(owner:WindowHandle):Void {
		var position = NativeKit.nk_window_get_position(owner);
		if (position.status != Result.Ok)
			return;
		var status = NativeKit.nk_window_set_bounds(window,
			position.out_x + WINDOW_POSITION_OFFSET,
			position.out_y + WINDOW_POSITION_OFFSET, width, height);
		if (status != Result.Ok)
			Sys.println("nativekit_ui_showcase window_chrome: could not place demo near owner: "
				+ NativeKit.nk_last_error());
	}

	function applySizeLimits():Void {
		var limits = new WindowSizeLimits();
		limits.set_min_width(MIN_WIDTH);
		limits.set_min_height(MIN_HEIGHT);
		limits.set_max_width(0);
		limits.set_max_height(0);
		var status = NativeKit.nk_window_set_size_limits(window, limits);
		if (status != Result.Ok)
			throw new NativeKitError(status, "window.set_size_limits", NativeKit.nk_last_error());
	}

	function buildRoot():View {
		var activeContext = context;
		if (activeContext == null)
			throw "Window chrome demo UI context is unavailable";
		var theme = activeContext.buildContext.theme;
		var rootStyle = new LayoutStyle();
		rootStyle.width = LayoutAxis.grow();
		rootStyle.height = LayoutAxis.grow();
		var contentStyle = new LayoutStyle();
		contentStyle.width = LayoutAxis.grow();
		contentStyle.height = LayoutAxis.grow();
		contentStyle.background = theme.panelBackground;
		var titleStyle = new LayoutStyle();
		titleStyle.width = LayoutAxis.grow();
		titleStyle.height = LayoutAxis.fixed(62.0);
		titleStyle.padding = new Insets(18.0, 18.0, 18.0, 18.0);
		titleStyle.direction = LayoutDirection.LeftToRight;
		titleStyle.childAlignY = LayoutAlignmentY.Center;
		titleStyle.childDistribution = LayoutDistribution.SpaceBetween;
		titleStyle.background = theme.buttonBackground;

		var controls = new Row("demo-controls", [
			new KeyedView("action", new WindowChrome("action-client",
				WindowDecorationRegionKind.Client, chromeButton(
					clientActionPressed ? "Client clicked" : "Client button", "client-action",
					function() { clientActionPressed = !clientActionPressed; }, theme))),
			new KeyedView("close", new WindowChrome("close-client",
				WindowDecorationRegionKind.Client, chromeButton("Close", "close-demo",
					function() { closeRequested = true; }, theme)))
		], buttonRowStyle(8.0));
		var titlebar = new WindowChrome("titlebar-drag", WindowDecorationRegionKind.Drag,
			new Row("titlebar", [
				new KeyedView("title", new Text("Window Chrome Demo",
					null, theme.buttonText)),
				new KeyedView("controls", controls)
			], titleStyle));

		var bodyStyle = new LayoutStyle();
		bodyStyle.width = LayoutAxis.grow();
		bodyStyle.height = LayoutAxis.grow();
		bodyStyle.padding = new Insets(34.0, 34.0, 34.0, 34.0);
		bodyStyle.childGap = 14.0;
		var body = new Column("demo-body", [
			new KeyedView("heading", new Text("Haxe owns the window frame",
				null, theme.heading.color)),
			new KeyedView("description", new Text(
				"Drag the blue header, click the client button, and resize from any edge or corner.",
				null, theme.caption.color)),
			new KeyedView("size", new Text('Client size: ${width} × ${height}',
				null, theme.accent)),
			new KeyedView("regions", new Text(
				"Drag · Client · ResizeNorth · ResizeSouth · ResizeEast · ResizeWest · corners",
				null, theme.caption.color))
		], bodyStyle);
		var base = new Column("demo-content", [
			new KeyedView("titlebar", titlebar),
			new KeyedView("body", body)
		], contentStyle);
		return new Stack("demo-root", [
			new StackChild("content", base),
			new StackChild("resize-zones", buildWindowResizeZones(), 0.0, 0.0, -1)
		], rootStyle);
	}

	function buildWindowResizeZones():Column {
		var edge = 7.0;
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
		var north = new Row("resize-north", [
			new KeyedView("northwest", resizeZone("northwest", WindowDecorationRegionKind.ResizeNorthwest,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge))),
			new KeyedView("north", resizeZone("north", WindowDecorationRegionKind.ResizeNorth,
				LayoutAxis.grow(), LayoutAxis.fixed(edge))),
			new KeyedView("northeast", resizeZone("northeast", WindowDecorationRegionKind.ResizeNortheast,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge)))
		], rowStyle);
		var middle = new Row("resize-middle", [
			new KeyedView("west", resizeZone("west", WindowDecorationRegionKind.ResizeWest,
				LayoutAxis.fixed(edge), LayoutAxis.grow())),
			new KeyedView("center", new SizedBox("resize-center", new Text(""),
				LayoutAxis.grow(), LayoutAxis.grow())),
			new KeyedView("east", resizeZone("east", WindowDecorationRegionKind.ResizeEast,
				LayoutAxis.fixed(edge), LayoutAxis.grow()))
		], middleStyle);
		var south = new Row("resize-south", [
			new KeyedView("southwest", resizeZone("southwest", WindowDecorationRegionKind.ResizeSouthwest,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge))),
			new KeyedView("south", resizeZone("south", WindowDecorationRegionKind.ResizeSouth,
				LayoutAxis.grow(), LayoutAxis.fixed(edge))),
			new KeyedView("southeast", resizeZone("southeast", WindowDecorationRegionKind.ResizeSoutheast,
				LayoutAxis.fixed(edge), LayoutAxis.fixed(edge)))
		], rowStyle);
		return new Column("resize-zones", [
			new KeyedView("north", north),
			new KeyedView("middle", middle),
			new KeyedView("south", south)
		], columnStyle);
	}

	function resizeZone(key:String, kind:WindowDecorationRegionKind,
			width:LayoutAxis, height:LayoutAxis):View
		return new WindowChrome(key, kind, new SizedBox(key + "-box", new Text(""), width, height));

	static function chromeButton(label:String, key:String, action:Void->Void, theme:Theme):Button {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fit();
		style.height = LayoutAxis.fixed(32.0);
		style.padding = new Insets(9.0, 10.0, 9.0, 10.0);
		style.background = theme.buttonBackground;
		style.radiusTopLeft = style.radiusTopRight = 4.0;
		style.radiusBottomLeft = style.radiusBottomRight = 4.0;
		return new Button(label, style, action, key);
	}

	static function buttonRowStyle(gap:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fit();
		style.height = LayoutAxis.fit();
		style.direction = LayoutDirection.LeftToRight;
		style.childGap = gap;
		return style;
	}

	public function dispose():Void {
		if (disposed)
			return;
		disposed = true;
		if (input != null)
			input.detach();
		if (frameSubscription != null)
			frameSubscription.dispose();
		if (context != null)
			context.dispose();
		if (renderer != null)
			renderer.dispose();
		if (nativeSurface != null) {
			nativeSurface.releaseBorrowed();
			nativeSurface = null;
		}
		if (surface.isValid())
			NativeKit.nk_surface_destroy(surface);
		if (window.isValid())
			NativeKit.nk_window_destroy(window);
		if (fonts != null)
			fonts.dispose();
	}
}
