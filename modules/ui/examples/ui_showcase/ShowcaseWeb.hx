import NativeKit;
import NativeKit.Key;
import NativeKit.Handle;
import NativeKit.NativeKitConstants;
import NativeKit.GraphicsApi;
import NativeKit.InputAction;
import NativeKit.Key;
import NativeKit.Result;
import NativeKit.InitOptions;
import NativeKit.WindowHandle;
import NativeKit.WindowOptions;
import NativeKit.WindowFlags;
import NativeKit.WindowKind;
import NativeKit.SurfaceHandle;
import NativeKit.SurfaceOptions;
import NativeKit.SurfaceFlags;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEvents.NativeKitEventSubscription;
import NativeKitSurface;
import haxe.io.Bytes;
import nativekit.ui.core.NativeInputAdapter;
import haxe.CallStack;

typedef ShowcaseWebFont = {
    var name:String;
    var uri:String;
    var bundledPath:String;
    var family:FontFamily;
};

/** Browser host entry points for the Haxeon UI Explorer wasm guest. */
class ShowcaseWeb {
    static var graphics:Null<Showcase>;
    static var explorer:Null<UiExplorer>;
    static var explorerInput:Null<NativeInputAdapter>;
    static var events:Null<NativeKitEvents>;
    static var eventSubscription:Null<NativeKitEventSubscription>;
    static var webFontBytes:Null<Map<String, Bytes>>;
    static var pendingWebFonts:Null<Map<String, ShowcaseWebFont>>;
    static var pendingWebFontCount = 0;
    static var initialized = false;
    static var running = false;
    static var ready = false;
    static var window:WindowHandle = WindowHandle.invalid();
    static var surface:SurfaceHandle = SurfaceHandle.invalid();
    static var logicalWidth:Float = 1200.0;
    static var logicalHeight:Float = 800.0;
    static var framebufferWidth = 0;
    static var framebufferHeight = 0;
    static var scale = 1.0;
    static var started = -1.0;
    static var rendered = 0;
    static var result = 0;
    static var failureStage = 0;
    static var requestedWidth = 1200;
    static var requestedHeight = 800;
    static var benchmarkScenario = 0;
    static var requestedMode = 0;
    static var requestedUiVisualCase = -1;
    static var graphicsMode = false;
    static var openGraphicsRequested = false;

    static function reportException(error:Dynamic, breadcrumbs:String):Void {
        var message = Std.string(error);
        var stack = CallStack.toString(CallStack.exceptionStack(true));
        NativeKitUIShowcase.nkui_showcase_diagnostic_report(failureStage,
            message == null ? "Unknown Haxe exception" : message,
            stack == null ? "" : stack, breadcrumbs);
    }

    public static function configure(width:Int, height:Int):Int {
        if (initialized || width <= 0 || height <= 0)
            return 1;
        requestedWidth = width;
        requestedHeight = height;
        logicalWidth = width;
        logicalHeight = height;
        return 0;
    }

    /** Selects the initial app: 0=UI Explorer, 1=focused Graphics Lab. */
    public static function configureMode(mode:Int):Int {
        if (initialized || (mode != 0 && mode != 1))
            return 1;
        requestedMode = mode;
        return 0;
    }

    /** Selects a deterministic UI Explorer screenshot state, 0-30. */
    public static function configureUiVisual(caseId:Int):Int {
        if (initialized || caseId < 0 || caseId > 30)
            return 1;
        requestedUiVisualCase = caseId;
        requestedMode = 0;
        return 0;
    }

    /** Selects a repeatable workload: 0=full, 1=static, 2=text-heavy, 3=text-editing. */
    public static function configureBenchmark(scenario:Int):Int {
        if (initialized || scenario < 0 || scenario > 3)
            return 1;
        benchmarkScenario = scenario;
        requestedMode = 1;
        return 0;
    }

    public static function main():Int {
        try {
            var init = new InitOptions();
            init.set_api_version(NativeKit.nk_api_version());
            init.set_event_queue_capacity(64);
            if (NativeKit.nk_init(init) != Result.Ok)
                return fail(10);
            initialized = true;

            graphicsMode = requestedMode == 1;
            var windowOptions = new WindowOptions();
            windowOptions.set_width(requestedWidth);
            windowOptions.set_height(requestedHeight);
            windowOptions.set_title("NativeKit UI Explorer");
            windowOptions.set_flags(WindowFlags.Resizable);
            windowOptions.set_owner(WindowHandle.invalid());
            windowOptions.set_kind(WindowKind.Normal);
            var createdWindow = NativeKit.nk_window_create(windowOptions);
            if (createdWindow.status != Result.Ok)
                return fail(11);
            window = createdWindow.out_window.borrow();

            var surfaceOptions = new SurfaceOptions();
            surfaceOptions.set_flags(SurfaceFlags.ForwardCompatible | SurfaceFlags.Stencil);
            surfaceOptions.set_api(GraphicsApi.OpenglEs);
            surfaceOptions.set_major_version(3);
            surfaceOptions.set_width(requestedWidth);
            surfaceOptions.set_height(requestedHeight);
            var createdSurface = NativeKit.nk_surface_create(new Handle(window.rawValue()), surfaceOptions);
            if (createdSurface.status != Result.Ok)
                return fail(12);
            surface = createdSurface.out_surface.borrow();
            var activePump = new NativeKitEvents();
            events = activePump;
            eventSubscription = activePump.listen(handleEvent);
            running = true;
            try {
                #if nativekit_bundle_web_fonts
                createShowcase(createWebFonts());
                #else
                startWebFontLoads();
                #end
            } catch (error:Dynamic) {
                failureStage = 91;
                reportException(error, "ShowcaseWeb.main > load-showcase-fonts");
                return fail(22);
            }
            return 0;
        } catch (error:Dynamic) {
            failureStage = 92;
            reportException(error, "ShowcaseWeb.main > initialize-runtime");
            return fail(20);
        }
    }

    /** Called by the browser host once per requestAnimationFrame tick. */
    public static function frame(time:Float):Int {
        if (!running)
            return result != 0 ? -result : 0;
        try {
            failureStage = 1;
            while (running && events != null && events.poll()) {}
            if (result != 0)
                return -result;

            if (openGraphicsRequested && explorer != null && !graphicsMode) {
                openGraphicsRequested = false;
                graphicsMode = true;
                if (explorerInput != null)
                    explorerInput.detach();
                graphics = new Showcase(createWebFonts());
                graphics.setViewport(logicalWidth, logicalHeight);
            }

            if (running && ready && (graphics != null || explorer != null)) {
                failureStage = 2;
                if (benchmarkScenario == 3)
                    if (graphics != null)
                        graphics.benchmarkTextEdit(rendered);
                if (NativeKit.nk_surface_make_current(surface) != Result.Ok)
                    return -fail(17);
                if (started < 0.0)
                    started = time;
                var elapsed = (time - started) / 1000.0;
                if (graphicsMode && graphics != null) {
                    failureStage = 3;
                    graphics.encodeFrame(elapsed, logicalWidth, logicalHeight, framebufferWidth,
                        framebufferHeight, scale, false);
                    graphics.render(surface, logicalWidth, logicalHeight, framebufferWidth,
                        framebufferHeight, scale);
                } else if (explorer != null) {
                    failureStage = 100 + explorer.getDiagnosticStage();
                    explorer.render(surface, elapsed);
                }
                failureStage = 4;
                if (NativeKit.nk_surface_present(surface) != Result.Ok)
                    return -fail(18);
                rendered++;
                failureStage = 0;
            }
            return running ? 1 : 0;
        } catch (error:UiError) {
            if (explorer != null && explorer.getDiagnosticStage() > 0)
                failureStage = 100 + explorer.getDiagnosticStage();
            else
                failureStage = 1000 + cast(error.status, Int);
            reportException(error, "ShowcaseWeb.frame > native-ui-operation: " + error.operation);
            return -fail(19);
        } catch (error:Dynamic) {
            if (explorer != null && explorer.getDiagnosticStage() > 0)
                failureStage = 100 + explorer.getDiagnosticStage();
            reportException(error, failureStage >= 130
                ? "ShowcaseWeb.frame > UiExplorer.render > UiContext.render"
                : "ShowcaseWeb.frame > render");
            return -fail(19);
        }
    }

    public static function status():Int
        return result != 0 ? result : (rendered > 0 ? 0 : 1);

    public static function diagnostic():Int
        return failureStage;

    public static function caretOffset():Int
        return graphics == null ? -1 : graphics.caretOffset();

    public static function caretAffinity():Int
        return graphics == null ? -1 : graphics.caretAffinity();

    public static function caretDirection():Int
        return graphics == null ? -1 : graphics.caretDirection();

    public static function shutdown():Void {
        running = false;
        ready = false;
        var eventPump = events;
        if (eventSubscription != null)
            eventSubscription.dispose();
        eventSubscription = null;
        events = null;
        if (graphics != null)
            graphics.dispose();
        graphics = null;
        if (explorerInput != null)
            explorerInput.detach();
        explorerInput = null;
        if (explorer != null)
            explorer.dispose();
        explorer = null;
        if (surface.isValid())
            NativeKit.nk_surface_destroy(surface);
        surface = SurfaceHandle.invalid();
        if (window.isValid())
            NativeKit.nk_window_destroy(window);
        window = WindowHandle.invalid();
        if (initialized)
            NativeKit.nk_shutdown();
        if (eventPump != null)
            eventPump.runtimeShutdown();
        webFontBytes = null;
        pendingWebFonts = null;
        pendingWebFontCount = 0;
        initialized = false;
    }

    static function fail(code:Int):Int {
        if (result == 0)
            result = code;
        running = false;
        return result;
    }

    static function createWebFonts():FontCollection {
        var fonts = FontCollection.create();
        try {
            for (font in webFontSpecs()) {
                #if nativekit_bundle_web_fonts
                fonts.add(font.bundledPath, font.family);
                #else
                if (webFontBytes == null)
                    throw "Web fonts have not finished loading";
                var data = webFontBytes.get(font.name);
                if (data == null)
                    throw "Missing downloaded web font " + font.name;
                fonts.addData(font.name, data, font.family);
                #end
            }
            return fonts;
        } catch (error:Dynamic) {
            fonts.dispose();
            throw error;
        }
    }

    static function webFontSpecs():Array<ShowcaseWebFont> return [
        {name: "IBMPlexSans-Regular", uri: "assets/IBMPlexSans-Regular.ttf",
            bundledPath: "/assets/IBMPlexSans-Regular.ttf", family: FontFamily.Default},
        {name: "IBMPlexSansArabic-Regular", uri: "assets/IBMPlexSansArabic-Regular.ttf",
            bundledPath: "/assets/IBMPlexSansArabic-Regular.ttf", family: FontFamily.Default},
        {name: "IBMPlexSansHebrew-Regular", uri: "assets/IBMPlexSansHebrew-Regular.ttf",
            bundledPath: "/assets/IBMPlexSansHebrew-Regular.ttf", family: FontFamily.Default},
        {name: "IBMPlexSansJP-Regular", uri: "assets/IBMPlexSansJP-Regular.ttf",
            bundledPath: "/assets/IBMPlexSansJP-Regular.ttf", family: FontFamily.Default},
        {name: "NotoEmoji-Regular", uri: "assets/NotoEmoji-Regular.ttf",
            bundledPath: "/assets/NotoEmoji-Regular.ttf", family: FontFamily.Emoji}
    ];

    static function startWebFontLoads():Void {
        webFontBytes = new Map();
        pendingWebFonts = new Map();
        pendingWebFontCount = 0;
        for (font in webFontSpecs()) {
            var resource = new Resource();
            resource.set_struct_size(Resource.size());
            resource.set_flags(NativeKit.ResourceFlags.Readable);
            resource.set_uri(font.uri);
            resource.set_mime_type("font/ttf");
            resource.set_display_name(font.name);
            var request = NativeKit.nk_resource_load_async_checked(resource);
            pendingWebFonts.set(Std.string(request), font);
            pendingWebFontCount++;
        }
    }

    static function createShowcase(fonts:FontCollection):Void {
        if (graphicsMode) {
            var sampleText:Null<String> = null;
            if (benchmarkScenario == 2) {
                var repeated = new StringBuf();
                for (_ in 0...8)
                    repeated.add("NativeKit — مرحبا — שלום — こんにちは 👋 · ");
                sampleText = repeated.toString();
            }
            graphics = new Showcase(fonts, sampleText);
            if (benchmarkScenario == 1)
                graphics.setBenchmarkAnimation(false);
        } else {
            explorer = new UiExplorer(fonts, "WEBGL2 · WASM", function() {
                openGraphicsRequested = true;
            });
            explorer.attachSurface(NativeKitSurface.borrowNativeHandle(surface));
            explorer.setViewport(logicalWidth, logicalHeight, requestedWidth,
                requestedHeight, scale);
            if (requestedUiVisualCase >= 0 && !explorer.setVisualCase(requestedUiVisualCase))
                throw "UI Explorer rejected the requested visual case";
            if (events != null)
                explorerInput = explorer.attachInput(events, new Handle(window.rawValue()));
        }
    }

    static function handleWebFontLoaded(request:haxe.Int64, loadResult:Result, data:Bytes):Void {
        if (pendingWebFonts == null)
            return;
        var key = Std.string(request);
        var font = pendingWebFonts.get(key);
        if (font == null)
            return;
        pendingWebFonts.remove(key);
        if (loadResult != Result.Ok || data.length == 0) {
            failureStage = 90;
            reportException("Failed to load web font " + font.uri,
                "ShowcaseWeb.font-load > " + font.name);
            fail(22);
            return;
        }
        if (webFontBytes == null)
            webFontBytes = new Map();
        webFontBytes.set(font.name, data);
        pendingWebFontCount--;
        if (pendingWebFontCount == 0) {
            pendingWebFonts = null;
            createShowcase(createWebFonts());
        }
    }

    static function handleEvent(value:NativeKitEventValue):Void {
        switch (value) {
            case Raw(kind, _, request, loadResult, _, _, data)
                if (kind == NativeKit.EventKind.ResourceDataComplete):
                handleWebFontLoaded(request, loadResult, data);
            case WindowClose(source) if (source.rawValue() == window.rawValue()):
                running = false;
            case WindowResize(source, width, height) if (source.rawValue() == window.rawValue()):
                if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) != Result.Ok) {
                    fail(13);
                    return;
                }
            case WindowScaleChanged(source, newScale) if (source.rawValue() == window.rawValue()):
                scale = newScale;
            case SurfaceReady(source) if (source.rawValue() == surface.rawValue()):
                if (NativeKit.nk_surface_make_current(surface) != Result.Ok) {
                    fail(14);
                    return;
                }
                var size = NativeKit.nk_surface_get_framebuffer_size(surface);
                if (size.status != Result.Ok) {
                    fail(15);
                    return;
                }
                framebufferWidth = size.out_width;
                framebufferHeight = size.out_height;
                var windowScale = NativeKit.nk_window_get_scale(window);
                if (windowScale.status != Result.Ok) {
                    fail(16);
                    return;
                }
                scale = windowScale.out_scale;
                if (explorer != null)
                    explorer.setViewport(logicalWidth, logicalHeight, framebufferWidth,
                        framebufferHeight, scale);
                ready = framebufferWidth > 0 && framebufferHeight > 0;
            case SurfaceResize(source, width, height, newFramebufferWidth, newFramebufferHeight)
                if (source.rawValue() == surface.rawValue()):
                logicalWidth = width;
                logicalHeight = height;
                framebufferWidth = newFramebufferWidth;
                framebufferHeight = newFramebufferHeight;
                ready = framebufferWidth > 0 && framebufferHeight > 0;
                if (explorer != null)
                    explorer.setViewport(logicalWidth, logicalHeight, framebufferWidth,
                        framebufferHeight, scale);
            case SurfaceLost(source) if (source.rawValue() == surface.rawValue()):
                ready = false;
            case PointerMove(source, x, y) if (source.rawValue() == window.rawValue() && graphicsMode && graphics != null):
                graphics.updatePointer(x, y);
            case PointerButton(source, _, action, _, x, y) if (source.rawValue() == window.rawValue() && graphicsMode && graphics != null):
                graphics.pointerButton(x, y, action == InputAction.Press);
            case Key(source, key, _, action, _) if (source.rawValue() == window.rawValue() &&
                    action == InputAction.Press && key == Key.Escape):
                if (graphicsMode && explorer != null) {
                    graphicsMode = false;
                    if (graphics != null) {
                        graphics.dispose();
                        graphics = null;
                    }
                    if (explorerInput != null && events != null)
                        explorerInput.attach(events);
                } else
                    running = false;
            case _:
        }
    }
}
