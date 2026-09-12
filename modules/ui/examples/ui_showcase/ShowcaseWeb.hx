import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKit.EventKind;
import NativeKit.GraphicsApi;
import NativeKit.InputAction;
import NativeKit.Result;
import NativeKit.InitOptions;
import NativeKit.SurfaceOptions;
import NativeKit.SurfaceFlags;
import NativeKitEvent;
import NativeKitEventValue;
import NativeKitOptions;

/** Browser host entry points for the Haxeon Showcase wasm guest. */
class ShowcaseWeb {
    static var app:Null<Showcase>;
    static var initialized = false;
    static var running = false;
    static var ready = false;
    static var window:Int = 0;
    static var surface:Int = 0;
    static var logicalWidth:Float = Showcase.LOGICAL_WIDTH;
    static var logicalHeight:Float = Showcase.LOGICAL_HEIGHT;
    static var framebufferWidth = 0;
    static var framebufferHeight = 0;
    static var scale = 1.0;
    static var started = -1.0;
    static var rendered = 0;
    static var result = 0;
    static var requestedWidth = 900;
    static var requestedHeight = 650;
    static var benchmarkScenario = 0;

    public static function configure(width:Int, height:Int):Int {
        if (initialized || width <= 0 || height <= 0)
            return 1;
        requestedWidth = width;
        requestedHeight = height;
        return 0;
    }

    /** Selects a repeatable workload: 0=full, 1=static, 2=text-heavy, 3=text-editing. */
    public static function configureBenchmark(scenario:Int):Int {
        if (initialized || scenario < 0 || scenario > 3)
            return 1;
        benchmarkScenario = scenario;
        return 0;
    }

    public static function main():Int {
        try {
            var init = new InitOptions();
            init.set_struct_size(InitOptions.size());
            init.set_api_version(NativeKit.nk_api_version());
            init.set_event_queue_capacity(64);
            if (NativeKit.nk_init(init) != Result.Ok)
                return fail(10);
            initialized = true;

            var windowOptions = NativeKitOptions.window(requestedWidth, requestedHeight,
                "NativeKit Haxeon Showcase");
            var createdWindow = NativeKit.nk_window_create(windowOptions);
            if (createdWindow.status != Result.Ok)
                return fail(11);
            window = createdWindow.out_window;

            var surfaceOptions = new SurfaceOptions();
            surfaceOptions.set_struct_size(SurfaceOptions.size());
            surfaceOptions.set_flags(SurfaceFlags.ForwardCompatible | SurfaceFlags.Stencil);
            surfaceOptions.set_api(GraphicsApi.OpenglEs);
            surfaceOptions.set_major_version(3);
            surfaceOptions.set_width(requestedWidth);
            surfaceOptions.set_height(requestedHeight);
            var createdSurface = NativeKit.nk_surface_create(window, surfaceOptions);
            if (createdSurface.status != Result.Ok)
                return fail(12);
            surface = createdSurface.out_surface;
            try {
                var fonts = FontCollection.create();
                fonts.add("/assets/IBMPlexSans-Regular.ttf");
                fonts.add("/assets/IBMPlexSansArabic-Regular.ttf");
                fonts.add("/assets/IBMPlexSansHebrew-Regular.ttf");
                fonts.add("/assets/IBMPlexSansJP-Regular.ttf");
                fonts.add("/assets/NotoEmoji-Regular.ttf", FontFamily.Emoji);
                var sampleText:Null<String> = null;
                if (benchmarkScenario == 2) {
                    var repeated = new StringBuf();
                    for (_ in 0...8)
                        repeated.add("NativeKit — مرحبا — שלום — こんにちは 👋 · ");
                    sampleText = repeated.toString();
                }
                app = new Showcase(fonts, sampleText);
                if (benchmarkScenario == 1)
                    app.setBenchmarkAnimation(false);
            } catch (error:Dynamic) {
                return fail(22);
            }
            running = true;
            return 0;
        } catch (error:Dynamic) {
            return fail(20);
        }
    }

    /** Called by the browser host once per requestAnimationFrame tick. */
    public static function frame(time:Float):Int {
        if (!running)
            return 0;
        try {
            var eventKind = EventKind.None;
            while (running) {
                var event = NativeKitEvent.poll();
                var context = event.snapshot();
                var value:NativeKitEventValue = event.kind == EventKind.None
                    ? None
                    : NativeKitEvent.decodeContext(context);
                eventKind = event.kind;
                event.release();

                // The browser backend emits surface-ready as a native event with no
                // payload; keep the explicit value mapping at this host boundary.
                if (eventKind == EventKind.SurfaceReady)
                    value = SurfaceReady(event.source);

                switch (value) {
                    case WindowClose(source) if (source == window):
                        running = false;
                    case WindowResize(source, width, height) if (source == window):
                        if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) != Result.Ok)
                            return -fail(13);
                    case SurfaceReady(source) if (source == surface):
                        if (NativeKit.nk_surface_make_current(surface) != Result.Ok)
                            return -fail(14);
                        var size = NativeKit.nk_surface_get_framebuffer_size(surface);
                        if (size.status != Result.Ok)
                            return -fail(15);
                        framebufferWidth = size.out_width;
                        framebufferHeight = size.out_height;
                        var windowScale = NativeKit.nk_window_get_scale(window);
                        if (windowScale.status != Result.Ok)
                            return -fail(16);
                        scale = windowScale.out_scale;
                        ready = framebufferWidth > 0 && framebufferHeight > 0;
                    case SurfaceResize(source, width, height, newFramebufferWidth, newFramebufferHeight)
                        if (source == surface):
                        logicalWidth = width;
                        logicalHeight = height;
                        framebufferWidth = newFramebufferWidth;
                        framebufferHeight = newFramebufferHeight;
                        ready = framebufferWidth > 0 && framebufferHeight > 0;
                    case SurfaceLost(source) if (source == surface):
                        ready = false;
                    case PointerMove(source, x, y) if (source == window && app != null):
                        app.updatePointer(x, y);
                    case PointerButton(source, _, action, _, x, y) if (source == window && app != null):
                        app.pointerButton(x, y, action == InputAction.Press);
                    case Key(source, key, _, action, _) if (source == window &&
                            action == InputAction.Press && key == NativeKitConstants.NK_KEY_ESCAPE):
                        running = false;
                    default:
                }
                if (eventKind == EventKind.None)
                    break;
            }

            if (running && ready && app != null) {
                if (benchmarkScenario == 3)
                    app.benchmarkTextEdit(rendered);
                if (NativeKit.nk_surface_make_current(surface) != Result.Ok)
                    return -fail(17);
                if (started < 0.0)
                    started = time;
                var elapsed = (time - started) / 1000.0;
                app.encodeFrame(elapsed, logicalWidth, logicalHeight, framebufferWidth,
                    framebufferHeight, scale, false);
                app.render(surface, logicalWidth, logicalHeight, framebufferWidth, framebufferHeight,
                    scale);
                if (NativeKit.nk_surface_present(surface) != Result.Ok)
                    return -fail(18);
                rendered++;
            }
            return running ? 1 : 0;
        } catch (error:Dynamic) {
            return -fail(19);
        }
    }

    public static function status():Int
        return result != 0 ? result : (rendered > 0 ? 0 : 1);

    public static function caretOffset():Int
        return app == null ? -1 : app.caretOffset();

    public static function caretAffinity():Int
        return app == null ? -1 : app.caretAffinity();

    public static function caretDirection():Int
        return app == null ? -1 : app.caretDirection();

    public static function shutdown():Void {
        running = false;
        ready = false;
        if (app != null)
            app.dispose();
        app = null;
        if (surface != 0)
            NativeKit.nk_surface_destroy(surface);
        surface = 0;
        if (window != 0)
            NativeKit.nk_window_destroy(window);
        window = 0;
        if (initialized)
            NativeKit.nk_shutdown();
        initialized = false;
    }

    static function fail(code:Int):Int {
        if (result == 0)
            result = code;
        running = false;
        return result;
    }
}
