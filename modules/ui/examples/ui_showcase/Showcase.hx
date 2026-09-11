import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKit.Nk_event_kind;
import NativeKit.Nk_graphics_api;
import NativeKit.Nk_input_action;
import NativeKit.Nk_result;
import NativeKitEvent;
import NativeKitEventValue;
import NativeKitOptions;
import haxe.io.Bytes;

/**
 * NativeKit Graphics Lab.
 *
 * This is intentionally written against the typed graphics API. The only
 * native handles in this file are the platform window and surface handles
 * needed to host the renderer; paths, paints, images, fonts, text layouts,
 * display lists, and renderer state stay behind their Haxe types.
 */
class Showcase {
    public static inline var LOGICAL_WIDTH:Float = 900.0;
    public static inline var LOGICAL_HEIGHT:Float = 650.0;
    public static inline var TARGET_FPS:Float = 60.0;
    public static inline var STATUS_REFRESH_SECONDS:Float = 0.25;

    final list:DisplayList;
    final renderer:Renderer;
    final fonts:FontCollection;
    final canvas:Canvas;
    final resources:Array<NativeKitUIResource>;

    final background:Paint;
    final lightBackground:Paint;
    final sidebar:Paint;
    final card:Paint;
    final cardRaised:Paint;
    final ink:Paint;
    final muted:Paint;
    final green:Paint;
    final cyan:Paint;
    final violet:Paint;
    final orange:Paint;
    final rose:Paint;

    final backgroundPath:Path;
    final sidebarPath:Path;
    final headerPath:Path;
    final vectorCardPath:Path;
    final paintCardPath:Path;
    final textCardPath:Path;
    final retainedCardPath:Path;
    final surfaceCardPath:Path;
    final starPath:Path;
    final curvePath:Path;
    final donutPath:Path;
    final dotPath:Path;
    final circle22Path:Path;
    final diamondPath:Path;
    final unitRectPath:Path;
    final animationButtonPath:Path;
    final sliderTrackPath:Path;
    final themeButtonPath:Path;
    final image:Image;

    final title:TextLayout;
    final subtitle:TextLayout;
    final vectorLabel:TextLayout;
    final paintLabel:TextLayout;
    final textLabel:TextLayout;
    final retainedLabel:TextLayout;
    final surfaceLabel:TextLayout;
    final multilingual:TextLayout;
    final sidebarCopy:TextLayout;
    final controlsLabel:TextLayout;
    final footerLabel:TextLayout;
    final animateOnLabel:TextLayout;
    final animateOffLabel:TextLayout;
    final darkThemeLabel:TextLayout;
    final lightThemeLabel:TextLayout;
    final curveLegendLabel:TextLayout;
    final paintCaption:TextLayout;
    final caretInstruction:TextLayout;
    final offscreenLabel:TextLayout;
    final targetLabel:TextLayout;

    var statusLayout:Null<TextLayout>;
    var caretStatusLayout:Null<TextLayout>;
    var caretPath:Null<Path>;
    var statusText:Null<String>;
    var statusLastRefresh:Float = -1.0;
    var statusFramebufferWidth:Int = -1;
    var statusFramebufferHeight:Int = -1;
    var statusPixelScale:Float = -1.0;
    var caretPathOffset:Int = -1;
    var caretPathAffinity:Int = -1;
    var caretStatusOffset:Int = -1;
    var caretStatusAffinity:Int = -1;
    var caretPosition:TextPosition;
    var pointerX:Float = 0.0;
    var pointerY:Float = 0.0;
    var layerOpacity:Float = 0.68;
    var animate:Bool = true;
    var lightTheme:Bool = false;
    var frameNumber:Int = 0;

    function new() {
        resources = [];
        list = DisplayList.create();
        renderer = Renderer.create();
        fonts = FontCollection.create();
        canvas = new Canvas(8192);
        fonts.addSystemFallbacks();

        background = keep(SolidPaint.create(Color.fromBytes(10, 15, 30)));
        lightBackground = keep(SolidPaint.create(Color.fromBytes(235, 240, 249)));
        sidebar = keep(SolidPaint.create(Color.fromBytes(17, 25, 47)));
        card = keep(SolidPaint.create(Color.fromBytes(24, 35, 62)));
        cardRaised = keep(SolidPaint.create(Color.fromBytes(31, 44, 77)));
        ink = keep(SolidPaint.create(Color.fromBytes(235, 242, 255)));
        muted = keep(SolidPaint.create(Color.fromBytes(151, 169, 202)));
        green = keep(SolidPaint.create(Color.fromBytes(50, 214, 143)));
        cyan = keep(SolidPaint.create(Color.fromBytes(52, 190, 238)));
        violet = keep(SolidPaint.create(Color.fromBytes(143, 105, 245)));
        orange = keep(SolidPaint.create(Color.fromBytes(247, 171, 76)));
        rose = keep(SolidPaint.create(Color.fromBytes(242, 104, 143)));

        backgroundPath = keep(rectPath(0.0, 0.0, LOGICAL_WIDTH, LOGICAL_HEIGHT));
        sidebarPath = keep(rectPath(0.0, 0.0, 220.0, LOGICAL_HEIGHT));
        headerPath = keep(roundRectPath(244.0, 24.0, 632.0, 64.0, 14.0));
        vectorCardPath = keep(roundRectPath(244.0, 108.0, 306.0, 236.0, 14.0));
        paintCardPath = keep(roundRectPath(564.0, 108.0, 312.0, 236.0, 14.0));
        textCardPath = keep(roundRectPath(244.0, 364.0, 632.0, 154.0, 14.0));
        retainedCardPath = keep(roundRectPath(244.0, 538.0, 306.0, 90.0, 14.0));
        surfaceCardPath = keep(roundRectPath(564.0, 538.0, 312.0, 90.0, 14.0));
        starPath = keep(star(0.0, 0.0, 34.0, 16.0, 7));
        curvePath = keep(curve());
        donutPath = keep(donut(0.0, 0.0, 23.0, 10.0));
        dotPath = keep(circle(0.0, 0.0, 7.0));
        circle22Path = keep(circle(0.0, 0.0, 22.0));
        diamondPath = keep(diamond(0.0, 0.0, 12.0));
        unitRectPath = keep(rectPath(0.0, 0.0, 1.0, 1.0));
        animationButtonPath = keep(rectPath(24.0, 350.0, 172.0, 40.0));
        sliderTrackPath = keep(rectPath(24.0, 424.0, 172.0, 6.0));
        themeButtonPath = keep(rectPath(24.0, 472.0, 172.0, 40.0));
        image = keep(checkerImage(24, 24));

        title = keep(TextLayout.createStyled(fonts, "NativeKit Graphics Lab",
            new TextStyle(29.0), new ParagraphStyle(600.0)));
        subtitle = keep(TextLayout.createStyled(fonts,
            "Typed Haxe graphics · retained display list · one compositor",
            new TextStyle(14.0), new ParagraphStyle(600.0)));
        vectorLabel = keep(TextLayout.createStyled(fonts, "VECTOR GRAPHICS",
            new TextStyle(12.0), new ParagraphStyle(270.0)));
        paintLabel = keep(TextLayout.createStyled(fonts, "PAINT + IMAGE",
            new TextStyle(12.0), new ParagraphStyle(270.0)));
        textLabel = keep(TextLayout.createStyled(fonts, "UNICODE TEXT + CARET",
            new TextStyle(12.0), new ParagraphStyle(580.0)));
        retainedLabel = keep(TextLayout.createStyled(fonts, "RETAINED PATH",
            new TextStyle(12.0), new ParagraphStyle(260.0)));
        surfaceLabel = keep(TextLayout.createStyled(fonts, "GRAPHICS SURFACE",
            new TextStyle(12.0), new ParagraphStyle(270.0)));
        multilingual = keep(TextLayout.createStyled(fonts,
            "NativeKit — مرحبا — שלום — こんにちは 👋",
            new TextStyle(18.0), new ParagraphStyle(580.0)));
        sidebarCopy = keep(TextLayout.createStyled(fonts,
            "A visual proof of the\nNativeKit rendering\narchitecture.",
            new TextStyle(16.0), new ParagraphStyle(180.0)));
        controlsLabel = keep(TextLayout.createStyled(fonts, "INTERACTIVE CONTROLS",
            new TextStyle(11.0), new ParagraphStyle(180.0)));
        footerLabel = keep(TextLayout.createStyled(fonts,
            "click the text card · move the pointer · resize the window",
            new TextStyle(11.0), new ParagraphStyle(560.0)));
        animateOnLabel = keep(TextLayout.createStyled(fonts, "ANIMATE  ·  ON",
            new TextStyle(11.0), new ParagraphStyle(150.0)));
        animateOffLabel = keep(TextLayout.createStyled(fonts, "ANIMATE  ·  OFF",
            new TextStyle(11.0), new ParagraphStyle(150.0)));
        darkThemeLabel = keep(TextLayout.createStyled(fonts, "THEME  ·  DARK",
            new TextStyle(11.0), new ParagraphStyle(150.0)));
        lightThemeLabel = keep(TextLayout.createStyled(fonts, "THEME  ·  LIGHT",
            new TextStyle(11.0), new ParagraphStyle(150.0)));
        curveLegendLabel = keep(TextLayout.createStyled(fonts,
            "Bezier · concave · caps / joins", new TextStyle(10.0), new ParagraphStyle(270.0)));
        paintCaption = keep(TextLayout.createStyled(fonts,
            "solid RGBA paints · filtered image · alpha layer", new TextStyle(10.0),
            new ParagraphStyle(270.0)));
        caretInstruction = keep(TextLayout.createStyled(fonts,
            "click to query code-point offset, affinity, direction, and caret geometry",
            new TextStyle(10.0), new ParagraphStyle(570.0)));
        offscreenLabel = keep(TextLayout.createStyled(fonts, "offscreen 2D producer",
            new TextStyle(10.0), new ParagraphStyle(230.0)));
        targetLabel = keep(TextLayout.createStyled(fonts, "3D-ready render-target slot",
            new TextStyle(10.0), new ParagraphStyle(230.0)));

        caretPosition = multilingual.hitTest(160.0, 22.0);
    }

    function keep<T:NativeKitUIResource>(resource:T):T {
        resources.push(resource);
        return resource;
    }

    public function updatePointer(x:Float, y:Float):Void {
        pointerX = x;
        pointerY = y;
        updateCaret();
    }

    public function pointerButton(x:Float, y:Float, pressed:Bool):Void {
        updatePointer(x, y);
        if (!pressed)
            return;
        if (x >= 24.0 && x <= 196.0 && y >= 350.0 && y <= 398.0) {
            animate = !animate;
        } else if (x >= 24.0 && x <= 196.0 && y >= 414.0 && y <= 454.0) {
            layerOpacity = Math.max(0.15, Math.min(1.0, (x - 24.0) / 172.0));
        } else if (x >= 24.0 && x <= 196.0 && y >= 472.0 && y <= 514.0) {
            lightTheme = !lightTheme;
        }
        updateCaret();
    }

    function updateCaret():Void {
        if (pointerX < 252.0 || pointerX > 868.0 || pointerY < 392.0 || pointerY > 510.0)
            return;
        caretPosition = multilingual.hitTest(pointerX - 260.0, pointerY - 397.0);
    }

    /** Encodes one complete frame using only the typed Haxe graphics API. */
    public function encodeFrame(seconds:Float, framebufferWidth:Int, framebufferHeight:Int,
            pixelScale:Float, staticFrame:Bool):Void {
        var replacedCaret:Null<Path> = null;
        var sizeChanged = framebufferWidth != statusFramebufferWidth ||
            framebufferHeight != statusFramebufferHeight || pixelScale != statusPixelScale;
        var refreshStatus = statusLayout == null || sizeChanged || staticFrame ||
            statusLastRefresh < 0.0 || seconds - statusLastRefresh >= STATUS_REFRESH_SECONDS;
        if (refreshStatus) {
            var stats = renderer.stats();
            var info = list.info();
            var status = 'OPENGL · ${framebufferWidth}×${framebufferHeight} · scale ${format(pixelScale)}\n' +
                'cmd ${info.commandCount} · ${info.commandBytes} B · prep ${stats.pathPreparations} · ' +
                'hits ${stats.pathCacheHits}\n' +
                'retained ${stats.pathGeometryBytesRetained} B';
            if (statusText == null || status != statusText) {
                if (statusLayout == null)
                    statusLayout = TextLayout.createStyled(fonts, status, new TextStyle(10.0),
                        new ParagraphStyle(240.0));
                else
                    statusLayout.setText(status);
                statusText = status;
            }
            statusLastRefresh = seconds;
            statusFramebufferWidth = framebufferWidth;
            statusFramebufferHeight = framebufferHeight;
            statusPixelScale = pixelScale;
        }

        var caretChanged = caretPath == null || caretPathOffset != caretPosition.offset ||
            caretPathAffinity != caretPosition.affinity;
        if (caretChanged) {
            var caret = multilingual.caret(caretPosition);
            replacedCaret = caretPath;
            caretPath = linePath(260.0 + caret.x, 397.0 + caret.y + caret.ascender,
                260.0 + caret.x, 397.0 + caret.y + caret.descender);
            caretPathOffset = caretPosition.offset;
            caretPathAffinity = caretPosition.affinity;
        }
        if (caretStatusLayout == null || caretStatusOffset != caretPosition.offset ||
            caretStatusAffinity != caretPosition.affinity) {
            var caretStatus = 'offset ${caretPosition.offset} · affinity ${caretPosition.affinity}';
            if (caretStatusLayout == null)
                caretStatusLayout = TextLayout.createStyled(fonts, caretStatus,
                    new TextStyle(10.0), new ParagraphStyle(570.0));
            else
                caretStatusLayout.setText(caretStatus);
            caretStatusOffset = caretPosition.offset;
            caretStatusAffinity = caretPosition.affinity;
        }

        canvas.reset();
        canvas.withState(function(canvas) {
            var base = lightTheme ? lightBackground : background;
            canvas.fill(backgroundPath, base);

            canvas.withClip(new Rect(0.0, 0.0, 220.0, LOGICAL_HEIGHT), function(canvas) {
                canvas.fill(sidebarPath, sidebar);
                canvas.setAlpha(0.9);
                canvas.withState(function(canvas) {
                    canvas.translate(28.0, 42.0);
                    canvas.fill(dotPath, green);
                });
                canvas.withState(function(canvas) {
                    canvas.translate(24.0, 68.0);
                    canvas.drawText(title, 0.0, 0.0);
                });
                canvas.drawText(sidebarCopy, 24.0, 134.0);
                canvas.drawText(controlsLabel, 24.0, 310.0);

                canvas.fill(animationButtonPath, animate ? green : cardRaised);
                canvas.drawText(animate ? animateOnLabel : animateOffLabel, 40.0, 375.0);
                canvas.fill(sliderTrackPath, cardRaised);
                canvas.withState(function(canvas) {
                    canvas.translate(24.0, 424.0);
                    canvas.scale(172.0 * layerOpacity, 6.0);
                    canvas.fill(unitRectPath, cyan);
                });
                canvas.withState(function(canvas) {
                    canvas.translate(24.0 + 172.0 * layerOpacity, 427.0);
                    canvas.fill(dotPath, ink);
                });
                canvas.fill(themeButtonPath, lightTheme ? orange : cardRaised);
                canvas.drawText(lightTheme ? lightThemeLabel : darkThemeLabel, 40.0, 497.0);
            });

            canvas.fill(headerPath, cardRaised);
            canvas.drawText(title, 270.0, 52.0);
            canvas.drawText(subtitle, 272.0, 82.0);
            canvas.drawText(statusLayout, 622.0, 39.0);

            canvas.fill(vectorCardPath, card);
            canvas.drawText(vectorLabel, 262.0, 132.0);
            canvas.withState(function(canvas) {
                canvas.translate(320.0, 218.0);
                canvas.rotate(staticFrame ? 0.0 : seconds * 0.45);
                canvas.fill(starPath, violet);
                canvas.stroke(starPath, ink, 2.0, LineCap.Round, LineJoin.Round);
            });
            canvas.withState(function(canvas) {
                canvas.translate(420.0, 218.0);
                canvas.fill(donutPath, cyan);
                canvas.stroke(donutPath, ink, 1.5, LineCap.Butt, LineJoin.Bevel);
            });
            canvas.withState(function(canvas) {
                canvas.translate(500.0, 218.0);
                canvas.rotate(staticFrame ? 0.0 : seconds * -0.6);
                canvas.stroke(curvePath, orange, 4.0, LineCap.Round, LineJoin.Miter);
            });
            canvas.drawText(curveLegendLabel, 264.0, 318.0);

            canvas.fill(paintCardPath, card);
            canvas.drawText(paintLabel, 582.0, 132.0);
            canvas.withState(function(canvas) {
                canvas.translate(610.0, 186.0);
                canvas.fill(circle22Path, green);
                canvas.translate(60.0, 0.0);
                canvas.fill(circle22Path, violet);
                canvas.translate(60.0, 0.0);
                canvas.fill(circle22Path, orange);
            });
            canvas.withClip(new Rect(594.0, 224.0, 252.0, 88.0), function(canvas) {
                canvas.withLayer(layerOpacity, function(canvas) {
                    canvas.drawImage(image, new Rect(600.0, 230.0, 84.0, 84.0));
                    canvas.withState(function(canvas) {
                        canvas.translate(790.0, 270.0);
                        canvas.fill(diamondPath, cyan);
                    });
                });
            });
            canvas.drawText(paintCaption, 582.0, 330.0);

            canvas.fill(textCardPath, cardRaised);
            canvas.drawText(textLabel, 262.0, 388.0);
            canvas.drawText(multilingual, 260.0, 425.0);
            canvas.drawText(caretInstruction, 260.0, 464.0);
            if (caretPath != null) {
                canvas.stroke(caretPath, green, 2.0, LineCap.Round, LineJoin.Round);
            }
            canvas.drawText(caretStatusLayout, 260.0, 495.0);

            canvas.fill(retainedCardPath, card);
            canvas.drawText(retainedLabel, 262.0, 559.0);
            canvas.withClip(new Rect(252.0, 568.0, 290.0, 54.0), function(canvas) {
                for (index in 0...18) {
                    canvas.withState(function(instance) {
                        instance.translate(265.0 + (index % 9) * 31.0,
                            594.0 + (index < 9 ? 0.0 : 16.0));
                        instance.setAlpha(0.25 + (index % 5) * 0.14);
                        instance.fill(starPath, index % 2 == 0 ? green : cyan);
                    });
                }
            });

            canvas.fill(surfaceCardPath, card);
            canvas.drawText(surfaceLabel, 582.0, 559.0);
            canvas.drawText(offscreenLabel, 582.0, 586.0);
            canvas.drawText(targetLabel, 582.0, 604.0);
            canvas.withLayer(0.75, function(canvas) {
                canvas.withState(function(canvas) {
                    canvas.translate(836.0, 583.0);
                    canvas.fill(diamondPath, rose);
                });
            });
            canvas.drawText(footerLabel, 262.0, 643.0);
        });
        canvas.update(list);
        if (replacedCaret != null)
            replacedCaret.dispose();
        frameNumber++;
    }

    public function render(surface:Int, logicalWidth:Float, logicalHeight:Float,
            framebufferWidth:Int, framebufferHeight:Int, pixelScale:Float):Void {
        var frame = new FrameInfo(logicalWidth, logicalHeight, framebufferWidth, framebufferHeight,
            pixelScale);
        renderer.renderFrame(list, Surface.fromNativeHandle(surface), frame);
    }

    public function printStats():Void {
        var stats = renderer.stats();
        var info = list.info();
        Sys.println("nativekit_ui_showcase");
        Sys.println("  display_list_commands=" + info.commandCount);
        Sys.println("  display_list_bytes=" + info.commandBytes);
        Sys.println("  path_preparations=" + stats.pathPreparations);
        Sys.println("  path_cache_hits=" + stats.pathCacheHits);
        Sys.println("  path_cache_misses=" + stats.pathCacheMisses);
        Sys.println("  path_geometry_bytes_retained=" + stats.pathGeometryBytesRetained);
    }

    public function dispose():Void {
        list.clear();
        if (caretPath != null) {
            caretPath.dispose();
            caretPath = null;
        }
        if (statusLayout != null) {
            statusLayout.dispose();
            statusLayout = null;
        }
        if (caretStatusLayout != null) {
            caretStatusLayout.dispose();
            caretStatusLayout = null;
        }
        renderer.dispose();
        list.dispose();
        for (resource in resources)
            resource.dispose();
        fonts.dispose();
    }

    function format(value:Float):String
        return Std.string(Math.round(value * 100.0) / 100.0);

    static function rectPath(x:Float, y:Float, width:Float, height:Float):Path
        return new PathBuilder().moveTo(x, y).lineTo(x + width, y).lineTo(x + width, y + height)
            .lineTo(x, y + height).close().build();

    static function roundRectPath(x:Float, y:Float, width:Float, height:Float, radius:Float):Path {
        var k = 0.5522848;
        return new PathBuilder().moveTo(x + radius, y).lineTo(x + width - radius, y)
            .cubicTo(x + width - radius + k * radius, y, x + width, y + radius - k * radius,
                x + width, y + radius)
            .lineTo(x + width, y + height - radius)
            .cubicTo(x + width, y + height - radius + k * radius, x + width - radius + k * radius,
                y + height, x + width - radius, y + height)
            .lineTo(x + radius, y + height)
            .cubicTo(x + radius - k * radius, y + height, x, y + height - radius + k * radius,
                x, y + height - radius)
            .lineTo(x, y + radius)
            .cubicTo(x, y + radius - k * radius, x + radius - k * radius, y, x + radius, y)
            .close().build();
    }

    static function circle(cx:Float, cy:Float, radius:Float):Path {
        var k = 0.5522848 * radius;
        return new PathBuilder().moveTo(cx + radius, cy).cubicTo(cx + radius, cy + k, cx + k,
            cy + radius, cx, cy + radius).cubicTo(cx - k, cy + radius, cx - radius, cy + k,
            cx - radius, cy).cubicTo(cx - radius, cy - k, cx - k, cy - radius, cx, cy - radius)
            .cubicTo(cx + k, cy - radius, cx + radius, cy - k, cx + radius, cy).close().build();
    }

    static function donut(cx:Float, cy:Float, outer:Float, inner:Float):Path {
        var k = 0.5522848;
        var b = new PathBuilder();
        appendCircle(b, cx, cy, outer, false, k);
        appendCircle(b, cx, cy, inner, true, k);
        return b.build();
    }

    static function appendCircle(b:PathBuilder, cx:Float, cy:Float, radius:Float,
            reverse:Bool, k:Float):Void {
        var s = reverse ? -1.0 : 1.0;
        b.moveTo(cx + radius, cy);
        b.cubicTo(cx + radius, cy + s * k * radius, cx + k * radius, cy + s * radius,
            cx, cy + s * radius);
        b.cubicTo(cx - k * radius, cy + s * radius, cx - radius, cy + s * k * radius,
            cx - radius, cy);
        b.cubicTo(cx - radius, cy - s * k * radius, cx - k * radius, cy - s * radius,
            cx, cy - s * radius);
        b.cubicTo(cx + k * radius, cy - s * radius, cx + radius, cy - s * k * radius,
            cx + radius, cy).close();
    }

    static function star(cx:Float, cy:Float, outer:Float, inner:Float, points:Int):Path {
        var b = new PathBuilder();
        for (index in 0...(points * 2)) {
            var radius = index % 2 == 0 ? outer : inner;
            var angle = -Math.PI * 0.5 + index * Math.PI / points;
            var x = cx + Math.cos(angle) * radius;
            var y = cy + Math.sin(angle) * radius;
            if (index == 0)
                b.moveTo(x, y);
            else
                b.lineTo(x, y);
        }
        return b.close().build();
    }

    static function diamond(cx:Float, cy:Float, radius:Float):Path
        return new PathBuilder().moveTo(cx, cy - radius).lineTo(cx + radius, cy)
            .lineTo(cx, cy + radius).lineTo(cx - radius, cy).close().build();

    static function curve():Path
        return new PathBuilder().moveTo(0.0, 28.0).cubicTo(24.0, -24.0, 56.0, 80.0, 82.0, 18.0)
            .cubicTo(98.0, -18.0, 118.0, 56.0, 142.0, 10.0).build();

    static function linePath(x1:Float, y1:Float, x2:Float, y2:Float):Path
        return new PathBuilder().moveTo(x1, y1).lineTo(x2, y2).build();

    static function checkerImage(width:Int, height:Int):Image {
        var pixels = Bytes.alloc(width * height * 4);
        for (y in 0...height)
            for (x in 0...width) {
                var bright = (((x / 4) + (y / 4)) % 2) == 0;
                var offset = (y * width + x) * 4;
                pixels.set(offset, bright ? 49 : 18);
                pixels.set(offset + 1, bright ? 202 : 76);
                pixels.set(offset + 2, bright ? 239 : 161);
                pixels.set(offset + 3, 255);
            }
        return Image.create(width, height, ImageFormat.RGBA8, pixels);
    }

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
        var window:Int = 0;
        var surface:Int = 0;
        var app:Null<Showcase> = null;
        var result = 0;
        try {
            var init = new nk_init_options();
            init.set_struct_size(nk_init_options.size());
            init.set_api_version(NativeKitConstants.NK_API_VERSION);
            init.set_event_queue_capacity(64);
            if (NativeKit.nk_init(init) != Nk_result.NK_OK)
                return 10;
            initialized = true;

            var windowOptions = NativeKitOptions.window(900, 650, "NativeKit Graphics Lab");
            var createdWindow = NativeKit.nk_window_create(windowOptions);
            if (createdWindow.status != Nk_result.NK_OK) {
                NativeKit.nk_shutdown();
                return 11;
            }
            window = createdWindow.out_window;

            var surfaceOptions = new nk_surface_options();
            surfaceOptions.set_struct_size(nk_surface_options.size());
            surfaceOptions.set_flags(NativeKitConstants.NK_SURFACE_FORWARD_COMPATIBLE |
                NativeKitConstants.NK_SURFACE_STENCIL);
            surfaceOptions.set_api(Nk_graphics_api.NK_GRAPHICS_OPENGL);
            surfaceOptions.set_major_version(3);
            surfaceOptions.set_minor_version(3);
            surfaceOptions.set_width(900);
            surfaceOptions.set_height(650);
            var createdSurface = NativeKit.nk_surface_create(window, surfaceOptions);
            if (createdSurface.status != Nk_result.NK_OK) {
                NativeKit.nk_window_destroy(window);
                NativeKit.nk_shutdown();
                return 12;
            }
            surface = createdSurface.out_surface;
            app = new Showcase();

            var running = true;
            var ready = false;
            var logicalWidth = LOGICAL_WIDTH;
            var logicalHeight = LOGICAL_HEIGHT;
            var framebufferWidth = 0;
            var framebufferHeight = 0;
            var scale = 1.0;
            var rendered = 0;
            var started = Date.now().getTime();
            var nextFrameAt:Float = started;

            while (running) {
                var event = NativeKitEvent.poll();
                var value = event.decode();
                var eventKind = event.kind;
                var eventSource = event.source;
                event.release();
                switch (value) {
                    case WindowClose(source) if (source == window):
                        running = false;
                    case WindowResize(source, width, height) if (source == window):
                        if (NativeKit.nk_surface_set_bounds(surface, 0, 0, width, height) !=
                            Nk_result.NK_OK)
                            throw "surface resize failed";
                    case SurfaceReady(source) if (source == surface):
                        ready = true;
                        var size = NativeKit.nk_surface_get_framebuffer_size(surface);
                        if (size.status != Nk_result.NK_OK)
                            throw "framebuffer size query failed";
                        framebufferWidth = size.out_width;
                        framebufferHeight = size.out_height;
                        var windowScale = NativeKit.nk_window_get_scale(window);
                        if (windowScale.status != Nk_result.NK_OK)
                            throw "window scale query failed";
                        scale = windowScale.out_scale;
                    case SurfaceResize(source, width, height, newFramebufferWidth, newFramebufferHeight)
                        if (source == surface):
                        logicalWidth = width;
                        logicalHeight = height;
                        framebufferWidth = newFramebufferWidth;
                        framebufferHeight = newFramebufferHeight;
                    case SurfaceLost(source) if (source == surface):
                        ready = false;
                    case PointerMove(source, x, y) if (source == window):
                        app.updatePointer(x, y);
                    case PointerButton(source, _, action, _, x, y) if (source == window):
                        app.pointerButton(x, y, action == Nk_input_action.NK_INPUT_PRESS);
                    case Key(source, key, _, action, _) if (source == window &&
                        action == Nk_input_action.NK_INPUT_PRESS && key == NativeKitConstants.NK_KEY_ESCAPE):
                        running = false;
                    default:
                }

                if (ready && running) {
                    if (!staticFrame && !smoke) {
                        var beforeFrame = Date.now().getTime();
                        if (nextFrameAt > beforeFrame)
                            Sys.sleep((nextFrameAt - beforeFrame) / 1000.0);
                    }
                    var elapsed = (Date.now().getTime() - started) / 1000.0;
                    if (staticFrame)
                        elapsed = 0.0;
                    app.encodeFrame(elapsed, framebufferWidth, framebufferHeight, scale, staticFrame);
                    app.render(surface, logicalWidth, logicalHeight, framebufferWidth, framebufferHeight,
                        scale);
                    if (NativeKit.nk_surface_present(surface) != Nk_result.NK_OK)
                        throw "surface present failed";
                    rendered++;
                    if (staticFrame || (smoke && rendered >= 30))
                        running = false;
                    else if (!smoke) {
                        nextFrameAt += 1000.0 / TARGET_FPS;
                        var afterFrame = Date.now().getTime();
                        if (nextFrameAt < afterFrame)
                            nextFrameAt = afterFrame;
                    }
                } else if (eventKind == Nk_event_kind.NK_EVENT_NONE) {
                    Sys.sleep(0.002);
                }
            }
            if (printStats || smoke || staticFrame)
                app.printStats();
            result = rendered > 0 ? 0 : 17;
        } catch (error:Dynamic) {
            Sys.println("nativekit_ui_showcase: " + Std.string(error));
            result = 20;
        }
        if (app != null)
            app.dispose();
        if (surface != 0)
            NativeKit.nk_surface_destroy(surface);
        if (window != 0)
            NativeKit.nk_window_destroy(window);
        if (initialized)
            NativeKit.nk_shutdown();
        return result;
    }
}
