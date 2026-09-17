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
import NativeKitError;
import nativekit.ui.core.NativeInputAdapter;

private class ShowcaseFrameState {
    public var running:Bool = true;
    public var ready:Bool = false;
    public var logicalWidth:Float = 0.0;
    public var logicalHeight:Float = 0.0;
    public var framebufferWidth:Int = 0;
    public var framebufferHeight:Int = 0;
    public var scale:Float = 1.0;
    public var rendered:Int = 0;
    public var callbackFailed:Bool = false;
    public var callbackError:Null<Dynamic> = null;
    public var graphicsMode:Bool = false;
    public var graphics:Null<Showcase> = null;
    public var explorer:Null<UiExplorer> = null;
}

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
        var uiStaticFrame = has(args, "--ui-static-frame");
        var staticFrame = has(args, "--static-frame");
        var printStats = has(args, "--stats");
        var graphicsMode = smoke || staticFrame;
        var initialWidth = graphicsMode ? 900 : UiExplorer.INITIAL_WIDTH;
        var initialHeight = graphicsMode ? 650 : UiExplorer.INITIAL_HEIGHT;
        for (arg in args)
            if (arg != "--smoke-test" && arg != "--ui-smoke-test" && arg != "--ui-static-frame" &&
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
        var nativeSurface:Null<NativeKitSurface> = null;
        var frameSubscription:Null<NativeKitSurfaceFrameSubscription> = null;
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
            windowOptions.set_title("Haxeon UI Explorer");
            windowOptions.set_flags(WindowFlags.Resizable | WindowFlags.Borderless);
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
            var frameSurface = NativeKitSurface.borrowNativeHandle(surface);
            nativeSurface = frameSurface;
            var running = true;
            var started = Date.now().getTime();
            var activePump = new NativeKitEvents();
            eventPump = activePump;
            var frameState = new ShowcaseFrameState();
            frameState.graphicsMode = graphicsMode;
            frameState.logicalWidth = initialWidth;
            frameState.logicalHeight = initialHeight;

            if (graphicsMode) {
                var fonts = FontCollection.create();
                try {
                    fonts.addSystemFallbacks();
                    graphics = new Showcase(fonts);
                } catch (error:Dynamic) {
                    fonts.dispose();
                    throw error;
                }
                graphics.setViewport(frameState.logicalWidth, frameState.logicalHeight);
            } else {
                var fonts = FontCollection.create();
                try {
                    fonts.addSystemFallbacks();
                    explorer = new UiExplorer(fonts,
                        '${platformName()} · ${graphicsApiName(graphicsApi)}', function() {
                        if (frameState.graphicsMode)
                            return;
                        frameState.graphicsMode = true;
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
                        graphics.setViewport(frameState.logicalWidth, frameState.logicalHeight);
                        frameState.graphics = graphics;
                    }, uiStaticFrame, Sys.getEnv("NKUI_SHOWCASE_IMAGE_PATH"));
                } catch (error:Dynamic) {
                    fonts.dispose();
                    throw error;
                }
                explorer.attachSurface(NativeKitSurface.borrowNativeHandle(surface));
                explorer.attachWindow(window);
                explorerInput = explorer.attachInput(activePump, new Handle(window.rawValue()));
                explorer.setViewport(frameState.logicalWidth, frameState.logicalHeight, initialWidth,
                    initialHeight, 1.0);
            }
            frameState.graphics = graphics;
            frameState.explorer = explorer;
            var renderFrame = function(framebufferWidth:Int, framebufferHeight:Int) {
                if (!frameState.ready || !frameState.running || framebufferWidth <= 0 ||
                        framebufferHeight <= 0)
                    return;
                frameState.framebufferWidth = framebufferWidth;
                frameState.framebufferHeight = framebufferHeight;
                var elapsed = (Date.now().getTime() - started) / 1000.0;
				if (staticFrame || uiStaticFrame)
					elapsed = 0.0;
                if (frameState.graphicsMode && frameState.graphics != null) {
                    var activeGraphics = frameState.graphics;
                    activeGraphics.encodeFrame(elapsed, frameState.logicalWidth, frameState.logicalHeight,
                        framebufferWidth, framebufferHeight, frameState.scale, staticFrame);
                    activeGraphics.render(surface, frameState.logicalWidth, frameState.logicalHeight,
                        framebufferWidth, framebufferHeight, frameState.scale);
                } else if (frameState.explorer != null) {
					if (uiSmoke)
						frameState.explorer.setSmokeFrame(frameState.rendered);
					else if (uiStaticFrame)
						frameState.explorer.setSmokeFrame(1);
                    frameState.explorer.setViewport(frameState.logicalWidth, frameState.logicalHeight,
                        framebufferWidth, framebufferHeight, frameState.scale);
                    frameState.explorer.render(surface, elapsed);
                }
                frameState.rendered++;
				if (staticFrame || ((smoke || uiSmoke || uiStaticFrame) && frameState.rendered >= 30))
                    frameState.running = false;
            };

            eventSubscription = activePump.listen(function(value) {
                switch (value) {
                    case WindowClose(source) if (source.rawValue() == window.rawValue()):
                        running = false;
                        frameState.running = false;
                    case WindowResize(source, width, height) if (source.rawValue() == window.rawValue()):
                        if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) !=
                            Result.Ok)
                            throw "surface resize failed";
                        frameState.logicalWidth = width;
                        frameState.logicalHeight = height;
                    case WindowScaleChanged(source, newScale) if (source.rawValue() == window.rawValue()):
                        frameState.scale = newScale;
                    case SurfaceReady(source) if (source.rawValue() == surface.rawValue()):
                        frameState.ready = true;
                        var size = NativeKit.nk_surface_get_framebuffer_size(surface);
                        if (size.status != Result.Ok)
                            throw "framebuffer size query failed";
                        frameState.framebufferWidth = size.out_width;
                        frameState.framebufferHeight = size.out_height;
                        var windowScale = NativeKit.nk_window_get_scale(window);
                        if (windowScale.status != Result.Ok)
                            throw "window scale query failed";
                        frameState.scale = windowScale.out_scale;
                        if (frameState.graphicsMode && graphics != null)
                            graphics.setViewport(frameState.logicalWidth, frameState.logicalHeight);
                        if (!frameState.graphicsMode && explorer != null)
                            explorer.setViewport(frameState.logicalWidth, frameState.logicalHeight,
                                frameState.framebufferWidth, frameState.framebufferHeight,
                                frameState.scale);
                        if (frameSubscription == null)
                            frameSubscription = frameSurface.onFrame(function(width, height) {
                                try {
                                    renderFrame(width, height);
                                } catch (error:Dynamic) {
                                    frameState.callbackFailed = true;
                                    frameState.callbackError = error;
                                    frameState.running = false;
                                }
                            });
                    case SurfaceResize(source, width, height, newFramebufferWidth, newFramebufferHeight)
                        if (source.rawValue() == surface.rawValue()):
                        frameState.logicalWidth = width;
                        frameState.logicalHeight = height;
                        frameState.framebufferWidth = newFramebufferWidth;
                        frameState.framebufferHeight = newFramebufferHeight;
                        if (frameState.graphicsMode && graphics != null)
                            graphics.setViewport(frameState.logicalWidth, frameState.logicalHeight);
                        if (!frameState.graphicsMode && explorer != null)
                            explorer.setViewport(frameState.logicalWidth, frameState.logicalHeight,
                                frameState.framebufferWidth, frameState.framebufferHeight,
                                frameState.scale);
                    case SurfaceLost(source) if (source.rawValue() == surface.rawValue()):
                        frameState.ready = false;
                    case PointerMove(source, x, y) if (source.rawValue() == window.rawValue()):
                        if (frameState.graphicsMode && graphics != null)
                            graphics.updatePointer(x, y);
                    case PointerButton(source, _, action, _, x, y) if (source.rawValue() == window.rawValue()):
                        if (frameState.graphicsMode && graphics != null)
                            graphics.pointerButton(x, y, action == InputAction.Press);
                    case Key(source, key, _, action, _) if (source.rawValue() == window.rawValue() &&
                            action == InputAction.Press && key == Key.Escape):
                        if (frameState.graphicsMode && explorer != null) {
                            frameState.graphicsMode = false;
                            if (graphics != null) {
                                graphics.dispose();
                                graphics = null;
                            }
                            frameState.graphics = null;
                            if (explorerInput != null)
                                explorerInput.attach(activePump);
                        } else {
                            running = false;
                            frameState.running = false;
                        }
                    case _:
                }
            });

			while (running) {
				var hadEvent = activePump.poll();
				if (frameState.callbackFailed) {
					var callbackError = frameState.callbackError;
					throw callbackError == null ? "surface frame callback failed" : callbackError;
				}
				if (!frameState.running)
					running = false;
				else if (!hadEvent)
					activePump.wait(1.0 / Showcase.TARGET_FPS);
			}
            if ((printStats || smoke || staticFrame) && graphics != null)
                graphics.printStats();
			if ((printStats || uiSmoke || uiStaticFrame) && explorer != null)
				explorer.printStats();
			if (uiSmoke || uiStaticFrame)
				Sys.println('nativekit_ui_showcase explorer_frames=${frameState.rendered}');
			result = frameState.rendered > 0 ? 0 : 17;
        } catch (error:Dynamic) {
            if (Std.isOfType(error, NativeKitError)) {
                var nativeError:NativeKitError = cast error;
                Sys.println('nativekit_ui_showcase: ${nativeError.operation} failed with result ${nativeError.result}'
                    + (nativeError.diagnostic == null || nativeError.diagnostic.length == 0
                        ? "" : ': ${nativeError.diagnostic}'));
            } else if (Std.isOfType(error, UiError)) {
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
        if (frameSubscription != null)
            frameSubscription.dispose();
        if (nativeSurface != null)
            nativeSurface.releaseBorrowed();
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
