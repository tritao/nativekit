import NativeKit;
import NativeKit.Key;
import NativeKit.NativeKitConstants;
import NativeKit.GraphicsApi;
import NativeKit.InputAction;
import NativeKit.Result;
import NativeKit.InitOptions;
import NativeKit.WindowHandle;
import NativeKit.SurfaceHandle;
import NativeKit.SurfaceOptions;
import NativeKit.SurfaceFlags;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitOptions;

/** Desktop frame-loop host for the shared Showcase scene. */
class ShowcaseDesktop {
    static function has(args:Array<String>, name:String):Bool
        return args.indexOf(name) >= 0;

    static function main():Int {
        var args = Sys.args();
        var smoke = has(args, "--smoke-test");
        var staticFrame = has(args, "--static-frame");
        var printStats = has(args, "--stats");
        for (arg in args)
            if (arg != "--smoke-test" && arg != "--static-frame" && arg != "--stats")
                return 2;

        var initialized = false;
        var window = WindowHandle.invalid();
        var surface = SurfaceHandle.invalid();
        var app:Null<Showcase> = null;
        var result = 0;
        try {
            var init = new InitOptions();
            init.set_api_version(NativeKit.nk_api_version());
            init.set_event_queue_capacity(64);
            if (NativeKit.nk_init(init) != Result.Ok)
                return 10;
            initialized = true;

            var windowOptions = NativeKitOptions.window(900, 650, "NativeKit Graphics Lab");
            var createdWindow = NativeKit.nk_window_create(windowOptions);
            if (createdWindow.status != Result.Ok) {
                NativeKit.nk_shutdown();
                return 11;
            }
            window = createdWindow.out_window;

            var surfaceOptions = new SurfaceOptions();
            surfaceOptions.set_flags(SurfaceFlags.ForwardCompatible | SurfaceFlags.Stencil);
            surfaceOptions.set_api(GraphicsApi.Opengl);
            surfaceOptions.set_major_version(3);
            surfaceOptions.set_minor_version(3);
            surfaceOptions.set_width(900);
            surfaceOptions.set_height(650);
            var createdSurface = NativeKit.nk_surface_create(window, surfaceOptions);
            if (createdSurface.status != Result.Ok) {
                NativeKit.nk_window_destroy(window);
                NativeKit.nk_shutdown();
                return 12;
            }
            surface = createdSurface.out_surface;
            var fonts = FontCollection.create();
            try {
                fonts.addSystemFallbacks();
                app = new Showcase(fonts);
            } catch (error:Dynamic) {
                fonts.dispose();
                throw error;
            }

            var running = true;
            var ready = false;
            var logicalWidth = Showcase.LOGICAL_WIDTH;
            var logicalHeight = Showcase.LOGICAL_HEIGHT;
            var framebufferWidth = 0;
            var framebufferHeight = 0;
            var scale = 1.0;
            var rendered = 0;
            var started = Date.now().getTime();
            var nextFrameAt:Float = started;
            app.setViewport(logicalWidth, logicalHeight);

            var events = new NativeKitEvents();
            events.addListener(function(value) {
                switch (value) {
                    case WindowClose(source) if (source.rawValue() == window.rawValue()):
                        running = false;
                    case WindowResize(source, width, height) if (source.rawValue() == window.rawValue()):
                        if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) !=
                            Result.Ok)
                            throw "surface resize failed";
                        app.setViewport(width, height);
                    case SurfaceReady(source) if (source.rawValue() == surface.rawValue()):
                        ready = true;
                        var size = NativeKit.nk_surface_get_framebuffer_size(surface);
                        if (size.status != Result.Ok)
                            throw "framebuffer size query failed";
                        framebufferWidth = size.out_width;
                        framebufferHeight = size.out_height;
                        var windowScale = NativeKit.nk_window_get_scale(window);
                        if (windowScale.status != Result.Ok)
                            throw "window scale query failed";
                        scale = windowScale.out_scale;
                    case SurfaceResize(source, width, height, newFramebufferWidth, newFramebufferHeight)
                        if (source.rawValue() == surface.rawValue()):
                        logicalWidth = width;
                        logicalHeight = height;
                        framebufferWidth = newFramebufferWidth;
                        framebufferHeight = newFramebufferHeight;
                        app.setViewport(logicalWidth, logicalHeight);
                    case SurfaceLost(source) if (source.rawValue() == surface.rawValue()):
                        ready = false;
                    case PointerMove(source, x, y) if (source.rawValue() == window.rawValue()):
                        app.updatePointer(x, y);
                    case PointerButton(source, _, action, _, x, y) if (source.rawValue() == window.rawValue()):
                        app.pointerButton(x, y, action == InputAction.Press);
                    case Key(source, key, _, action, _) if (source.rawValue() == window.rawValue() &&
                            action == InputAction.Press && key == Key.Escape):
                        running = false;
                    case _:
                }
            });

            while (running) {
                var hadEvent = events.poll();

                if (ready && running) {
                    if (!staticFrame && !smoke) {
                        var beforeFrame = Date.now().getTime();
                        if (nextFrameAt > beforeFrame)
                            Sys.sleep((nextFrameAt - beforeFrame) / 1000.0);
                    }
                    var elapsed = (Date.now().getTime() - started) / 1000.0;
                    if (staticFrame)
                        elapsed = 0.0;
                    app.encodeFrame(elapsed, logicalWidth, logicalHeight, framebufferWidth,
                        framebufferHeight, scale, staticFrame);
                    app.render(surface, logicalWidth, logicalHeight, framebufferWidth, framebufferHeight,
                        scale);
                    if (NativeKit.nk_surface_present(surface) != Result.Ok)
                        throw "surface present failed";
                    rendered++;
                    if (staticFrame || (smoke && rendered >= 30))
                        running = false;
                    else if (!smoke) {
                        nextFrameAt += 1000.0 / Showcase.TARGET_FPS;
                        var afterFrame = Date.now().getTime();
                        if (nextFrameAt < afterFrame)
                            nextFrameAt = afterFrame;
                    }
                } else if (!hadEvent) {
                    Sys.sleep(0.002);
                }
            }
            if (printStats || smoke || staticFrame)
                app.printStats();
            result = rendered > 0 ? 0 : 17;
        } catch (error:Dynamic) {
            if (Std.isOfType(error, UiError)) {
                var uiError:UiError = cast error;
                Sys.println('nativekit_ui_showcase: ${uiError.operation} failed with status ${uiError.status}');
            } else
                Sys.println("nativekit_ui_showcase: " + Std.string(error));
            result = 20;
        }
        if (app != null)
            app.dispose();
        if (surface.isValid())
            NativeKit.nk_surface_destroy(surface);
        if (window.isValid())
            NativeKit.nk_window_destroy(window);
        if (initialized)
            NativeKit.nk_shutdown();
        return result;
    }
}
