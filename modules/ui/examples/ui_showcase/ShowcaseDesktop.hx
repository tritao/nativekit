import NativeKit;
import NativeKit.Key;
import NativeKit.Handle;
import NativeKit.NativeKitConstants;
import NativeKit.GraphicsApi;
import NativeKitGpu;
import NativeKit.InputAction;
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
import nativekit.ui.core.NativeInputAdapter;

/** Desktop frame-loop host for the NativeKit UI Explorer. */
class ShowcaseDesktop {
    static function platformName():String {
        var name = Sys.systemName();
        return switch (name) {
            case "Windows": "WINDOWS";
            case "Mac": "MACOS";
            case "Linux": "LINUX";
            default: name.toUpperCase();
        };
    }

    static function graphicsApiName(api:GraphicsApi):String {
        return switch (api) {
            case GraphicsApi.D3d11: "D3D11";
            case GraphicsApi.Metal: "METAL";
            case GraphicsApi.OpenglEs: "GLES";
            case GraphicsApi.Vulkan: "VULKAN";
            default: "OPENGL";
        };
    }

    static function has(args:Array<String>, name:String):Bool
        return args.indexOf(name) >= 0;

    static function main():Int {
        var args = Sys.args();
        var smoke = has(args, "--smoke-test");
        var uiSmoke = has(args, "--ui-smoke-test");
        var staticFrame = has(args, "--static-frame");
        var printStats = has(args, "--stats");
        var graphicsMode = smoke || staticFrame;
        var initialWidth = graphicsMode ? 900 : 1100;
        var initialHeight = graphicsMode ? 650 : 760;
        for (arg in args)
            if (arg != "--smoke-test" && arg != "--ui-smoke-test" &&
                    arg != "--static-frame" && arg != "--stats")
                return 2;

        var initialized = false;
        var eventPump:Null<NativeKitEvents> = null;
        var eventSubscription:Null<NativeKitEventSubscription> = null;
        var window = WindowHandle.invalid();
        var surface = SurfaceHandle.invalid();
        var graphics:Null<Showcase> = null;
        var explorer:Null<UiExplorer> = null;
        var explorerInput:Null<NativeInputAdapter> = null;
        var result = 0;
        try {
            var init = new InitOptions();
            init.set_api_version(NativeKit.nk_api_version());
            init.set_event_queue_capacity(64);
            if (NativeKit.nk_init(init) != Result.Ok)
                return 10;
            initialized = true;

            var windowOptions = new WindowOptions();
            windowOptions.set_width(initialWidth);
            windowOptions.set_height(initialHeight);
            windowOptions.set_title("NativeKit UI Explorer");
            windowOptions.set_flags(WindowFlags.Resizable);
            windowOptions.set_owner(WindowHandle.invalid());
            windowOptions.set_kind(WindowKind.Normal);
            var createdWindow = NativeKit.nk_window_create(windowOptions);
            if (createdWindow.status != Result.Ok) {
                NativeKit.nk_shutdown();
                return 11;
            }
            window = createdWindow.out_window.borrow();

            var surfaceOptions = new SurfaceOptions();
            var graphicsApi:GraphicsApi = NativeKitGpu.nkgpu_default_graphics_api();
            var surfaceFlags = SurfaceFlags.Stencil;
            if (graphicsApi == GraphicsApi.Opengl) {
                surfaceFlags = SurfaceFlags.ForwardCompatible | SurfaceFlags.Stencil;
                surfaceOptions.set_major_version(3);
                surfaceOptions.set_minor_version(3);
            }
            surfaceOptions.set_flags(surfaceFlags);
            surfaceOptions.set_api(graphicsApi);
            surfaceOptions.set_width(initialWidth);
            surfaceOptions.set_height(initialHeight);
            var createdSurface = NativeKit.nk_surface_create(new Handle(window.rawValue()), surfaceOptions);
            if (createdSurface.status != Result.Ok) {
                NativeKit.nk_window_destroy(window);
                NativeKit.nk_shutdown();
                return 12;
            }
            surface = createdSurface.out_surface.borrow();
            var running = true;
            var ready = false;
            var logicalWidth:Float = initialWidth;
            var logicalHeight:Float = initialHeight;
            var framebufferWidth = 0;
            var framebufferHeight = 0;
            var scale = 1.0;
            var rendered = 0;
            var started = Date.now().getTime();
            var nextFrameAt:Float = started;
            var activePump = new NativeKitEvents();
            eventPump = activePump;

            if (graphicsMode) {
                var fonts = FontCollection.create();
                try {
                    fonts.addSystemFallbacks();
                    graphics = new Showcase(fonts);
                } catch (error:Dynamic) {
                    fonts.dispose();
                    throw error;
                }
                graphics.setViewport(logicalWidth, logicalHeight);
            } else {
                var fonts = FontCollection.create();
                try {
                    fonts.addSystemFallbacks();
                    explorer = new UiExplorer(fonts,
                        '${platformName()} · ${graphicsApiName(graphicsApi)}', function() {
                        if (graphicsMode)
                            return;
                        graphicsMode = true;
                        if (explorerInput != null)
                            explorerInput.detach();
                        var graphicsFonts = FontCollection.create();
                        try {
                            graphicsFonts.addSystemFallbacks();
                            graphics = new Showcase(graphicsFonts);
                        } catch (error:Dynamic) {
                            graphicsFonts.dispose();
                            throw error;
                        }
                        graphics.setViewport(logicalWidth, logicalHeight);
                    });
                } catch (error:Dynamic) {
                    fonts.dispose();
                    throw error;
                }
                explorer.attachSurface(NativeKitSurface.borrowNativeHandle(surface));
                explorerInput = explorer.attachInput(activePump, new Handle(window.rawValue()));
                explorer.setViewport(logicalWidth, logicalHeight, initialWidth, initialHeight, 1.0);
            }

            eventSubscription = activePump.listen(function(value) {
                switch (value) {
                    case WindowClose(source) if (source.rawValue() == window.rawValue()):
                        running = false;
                    case WindowResize(source, width, height) if (source.rawValue() == window.rawValue()):
                        if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) !=
                            Result.Ok)
                            throw "surface resize failed";
                        logicalWidth = width;
                        logicalHeight = height;
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
                        if (graphicsMode && graphics != null)
                            graphics.setViewport(logicalWidth, logicalHeight);
                        if (!graphicsMode && explorer != null)
                            explorer.setViewport(logicalWidth, logicalHeight, framebufferWidth,
                                framebufferHeight, scale);
                    case SurfaceResize(source, width, height, newFramebufferWidth, newFramebufferHeight)
                        if (source.rawValue() == surface.rawValue()):
                        logicalWidth = width;
                        logicalHeight = height;
                        framebufferWidth = newFramebufferWidth;
                        framebufferHeight = newFramebufferHeight;
                        if (graphicsMode && graphics != null)
                            graphics.setViewport(logicalWidth, logicalHeight);
                        if (!graphicsMode && explorer != null)
                            explorer.setViewport(logicalWidth, logicalHeight, framebufferWidth,
                                framebufferHeight, scale);
                    case SurfaceLost(source) if (source.rawValue() == surface.rawValue()):
                        ready = false;
                    case PointerMove(source, x, y) if (source.rawValue() == window.rawValue()):
                        if (graphicsMode && graphics != null)
                            graphics.updatePointer(x, y);
                    case PointerButton(source, _, action, _, x, y) if (source.rawValue() == window.rawValue()):
                        if (graphicsMode && graphics != null)
                            graphics.pointerButton(x, y, action == InputAction.Press);
                    case Key(source, key, _, action, _) if (source.rawValue() == window.rawValue() &&
                            action == InputAction.Press && key == Key.Escape):
                        if (graphicsMode && explorer != null) {
                            graphicsMode = false;
                            if (graphics != null) {
                                graphics.dispose();
                                graphics = null;
                            }
                            if (explorerInput != null)
                                explorerInput.attach(activePump);
                        } else
                            running = false;
                    case _:
                }
            });

            while (running) {
                var hadEvent = activePump.poll();

                if (ready) {
                    var currentFramebuffer = NativeKit.nk_surface_get_framebuffer_size(surface);
                    if (currentFramebuffer.status != Result.Ok)
                        throw "framebuffer size query failed";
                    framebufferWidth = currentFramebuffer.out_width;
                    framebufferHeight = currentFramebuffer.out_height;
                }

                if (ready && running && framebufferWidth > 0 && framebufferHeight > 0) {
                    if (!staticFrame && !smoke && !uiSmoke) {
                        var beforeFrame = Date.now().getTime();
                        if (nextFrameAt > beforeFrame)
                            Sys.sleep((nextFrameAt - beforeFrame) / 1000.0);
                    }
                    var elapsed = (Date.now().getTime() - started) / 1000.0;
                    if (staticFrame)
                        elapsed = 0.0;
                    if (graphicsMode && graphics != null) {
                        graphics.encodeFrame(elapsed, logicalWidth, logicalHeight, framebufferWidth,
                            framebufferHeight, scale, staticFrame);
                        graphics.render(surface, logicalWidth, logicalHeight, framebufferWidth,
                            framebufferHeight, scale);
                    } else if (explorer != null) {
                        if (uiSmoke)
                            explorer.setSmokeFrame(rendered);
                        explorer.render(surface, elapsed);
                    }
                    if (NativeKit.nk_surface_present(surface) != Result.Ok)
                        throw "surface present failed";
                    rendered++;
                    if (staticFrame || ((smoke || uiSmoke) && rendered >= 30))
                        running = false;
                    else if (!smoke && !uiSmoke) {
                        nextFrameAt += 1000.0 / Showcase.TARGET_FPS;
                        var afterFrame = Date.now().getTime();
                        if (nextFrameAt < afterFrame)
                            nextFrameAt = afterFrame;
                    }
                } else if (!hadEvent) {
                    Sys.sleep(0.002);
                }
            }
            if ((printStats || smoke || staticFrame) && graphics != null)
                graphics.printStats();
            if (uiSmoke)
                Sys.println('nativekit_ui_showcase explorer_frames=$rendered');
            result = rendered > 0 ? 0 : 17;
        } catch (error:Dynamic) {
            if (Std.isOfType(error, UiError)) {
                var uiError:UiError = cast error;
                Sys.println('nativekit_ui_showcase: ${uiError.operation} failed with status ${uiError.status}');
            } else
                Sys.println("nativekit_ui_showcase: " + Std.string(error));
            result = 20;
        }
        if (graphics != null)
            graphics.dispose();
        if (explorerInput != null)
            explorerInput.detach();
        if (explorer != null)
            explorer.dispose();
        if (eventSubscription != null)
            eventSubscription.dispose();
        if (surface.isValid())
            NativeKit.nk_surface_destroy(surface);
        if (window.isValid())
            NativeKit.nk_window_destroy(window);
        if (initialized)
            NativeKit.nk_shutdown();
        if (eventPump != null)
            eventPump.runtimeShutdown();
        return result;
    }
}
