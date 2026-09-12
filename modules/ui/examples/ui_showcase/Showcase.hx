import NativeKit;
import NativeKitUI;
import NativeKit.NativeKitConstants;
import NativeKit.EventKind;
import NativeKit.GraphicsApi;
import NativeKit.InputAction;
import NativeKit.Result;
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
    static inline var HEADER_X:Float = 244.0;
    static inline var HEADER_Y:Float = 20.0;
    static inline var HEADER_WIDTH:Float = 632.0;
    static inline var HEADER_HEIGHT:Float = 76.0;
    static inline var HEADER_TITLE_X:Float = 270.0;
    static inline var HEADER_TITLE_Y:Float = 46.0;
    static inline var HEADER_SUBTITLE_X:Float = 272.0;
    static inline var HEADER_SUBTITLE_Y:Float = 74.0;
    static inline var HEADER_STATUS_X:Float = 622.0;
    static inline var HEADER_STATUS_Y:Float = 34.0;
    static inline var TEXT_ORIGIN_X:Float = 260.0;
    static inline var TEXT_ORIGIN_Y:Float = 425.0;
    static inline var IMAGE_TEXTURE_SIZE:Int = 256;
    static inline var IMAGE_TEXTURE_CELLS:Int = 6;
    public static inline var TARGET_FPS:Float = 60.0;
    public static inline var STATUS_REFRESH_SECONDS:Float = 0.25;

    final list:DisplayList;
    final renderer:Renderer;
    final fonts:FontCollection;
    final layoutSession:LayoutSession;
    final layoutRoot:LayoutNode;
    final layoutButton:LayoutNode;
    final layoutLabel:LayoutNode;
    final layoutFrame:LayoutFrame;
    final canvas:Canvas;
    final frameInfo:FrameInfo;
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
    var pointerDown:Bool = false;
    var pointerPressedPending:Bool = false;
    var layerOpacity:Float = 0.68;
    var animate:Bool = true;
    var lightTheme:Bool = false;
    var sceneScale:Float = 1.0;
    var sceneOffsetX:Float = 0.0;
    var sceneOffsetY:Float = 0.0;

    /** Takes ownership of the configured font collection. */
    function new(fonts:FontCollection, ?multilingualText:String) {
        resources = [];
        list = DisplayList.create();
        renderer = Renderer.create();
        this.fonts = fonts;
        canvas = new Canvas(8192);

        layoutSession = LayoutSession.create();
        layoutSession.setFonts(fonts);
        layoutFrame = new LayoutFrame(LOGICAL_WIDTH, LOGICAL_HEIGHT);
        frameInfo = new FrameInfo(LOGICAL_WIDTH, LOGICAL_HEIGHT, Std.int(LOGICAL_WIDTH),
            Std.int(LOGICAL_HEIGHT), 1.0);
        layoutRoot = LayoutNode.box(9001);
        layoutButton = LayoutNode.button(9002);
        layoutLabel = LayoutNode.textNode(9003, "");
        layoutButton.style.width = LayoutAxis.fixed(210.0);
        layoutButton.style.height = LayoutAxis.fixed(42.0);
        layoutButton.style.padding = new Insets(10.0, 8.0, 10.0, 8.0);
        layoutButton.style.background = Color.rgba(0.08, 0.18, 0.32, 0.94);
        layoutButton.style.radiusTopLeft = 9.0;
        layoutButton.style.radiusTopRight = 9.0;
        layoutButton.style.radiusBottomLeft = 9.0;
        layoutButton.style.radiusBottomRight = 9.0;
        layoutLabel.textColor = Color.rgba(0.35, 0.95, 0.72, 1.0);
        layoutLabel.textStyle.fontSize = 11.0;
        layoutButton.add(layoutLabel);
        layoutRoot.add(layoutButton);

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
        headerPath = keep(roundRectPath(HEADER_X, HEADER_Y, HEADER_WIDTH, HEADER_HEIGHT, 14.0));
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
        // Keep enough source resolution for the image panel at high-DPI scales.
        // The renderer remains linear-filtered; this avoids enlarging a tiny
        // diagnostic bitmap until its source texels become visible.
        image = keep(checkerImage(IMAGE_TEXTURE_SIZE, IMAGE_TEXTURE_SIZE));

        title = styled("NativeKit Graphics Lab", 600.0, 29.0);
        subtitle = styled("Typed Haxe graphics · retained display list · one compositor", 600.0, 14.0);
        vectorLabel = styled("VECTOR GRAPHICS", 270.0, 12.0);
        paintLabel = styled("PAINT + IMAGE", 270.0, 12.0);
        textLabel = styled("UNICODE TEXT + CARET", 580.0, 12.0);
        retainedLabel = styled("RETAINED PATH", 260.0, 12.0);
        surfaceLabel = styled("GRAPHICS SURFACE", 270.0, 12.0);
        multilingual = styled(multilingualText == null ? "NativeKit — مرحبا — שלום — こんにちは 👋" : multilingualText,
            580.0, 18.0);
        sidebarCopy = styled("A visual proof of the\nNativeKit rendering\narchitecture.", 180.0, 16.0);
        controlsLabel = styled("INTERACTIVE CONTROLS", 180.0, 11.0);
        footerLabel = styled("click the text card · move the pointer · resize the window", 560.0, 11.0);
        animateOnLabel = styled("ANIMATE  ·  ON", 150.0, 11.0);
        animateOffLabel = styled("ANIMATE  ·  OFF", 150.0, 11.0);
        darkThemeLabel = styled("THEME  ·  DARK", 150.0, 11.0);
        lightThemeLabel = styled("THEME  ·  LIGHT", 150.0, 11.0);
        curveLegendLabel = styled("Bezier · concave · caps / joins", 270.0, 10.0);
        paintCaption = styled("solid RGBA paints · filtered image · alpha layer", 270.0, 10.0);
        caretInstruction = styled("click to query code-point offset, affinity, direction, and caret geometry",
            570.0, 10.0);
        offscreenLabel = styled("offscreen 2D producer", 230.0, 10.0);
        targetLabel = styled("3D-ready render-target slot", 230.0, 10.0);

        caretPosition = multilingual.hitTest(160.0, 22.0);
    }

    /** Fits the fixed design canvas into the current logical window viewport. */
    public function setViewport(width:Float, height:Float):Void {
        if (width <= 0.0 || height <= 0.0)
            return;
        sceneScale = Math.min(width / LOGICAL_WIDTH, height / LOGICAL_HEIGHT);
        sceneOffsetX = (width - LOGICAL_WIDTH * sceneScale) * 0.5;
        sceneOffsetY = (height - LOGICAL_HEIGHT * sceneScale) * 0.5;
    }

    function keep<T:NativeKitUIResource>(resource:T):T {
        resources.push(resource);
        return resource;
    }

    function styled(value:String, width:Float, fontSize:Float):TextLayout
        return keep(TextLayout.createStyled(fonts, value, width, new TextStyle(fontSize),
            new ParagraphStyle()));

    public function updatePointer(x:Float, y:Float):Void {
        pointerX = (x - sceneOffsetX) / sceneScale;
        pointerY = (y - sceneOffsetY) / sceneScale;
        updateCaret();
    }

    public function pointerButton(x:Float, y:Float, pressed:Bool):Void {
        updatePointer(x, y);
        pointerDown = pressed;
        if (pressed)
            pointerPressedPending = true;
        if (!pressed)
            return;
        if (pointerX >= 24.0 && pointerX <= 196.0 && pointerY >= 350.0 && pointerY <= 398.0) {
            animate = !animate;
        } else if (pointerX >= 24.0 && pointerX <= 196.0 && pointerY >= 414.0 &&
                pointerY <= 454.0) {
            layerOpacity = Math.max(0.15, Math.min(1.0, (pointerX - 24.0) / 172.0));
        } else if (pointerX >= 24.0 && pointerX <= 196.0 && pointerY >= 472.0 &&
                pointerY <= 514.0) {
            lightTheme = !lightTheme;
        }
        updateCaret();
    }

    function submitLayoutFrame(logicalWidth:Float, logicalHeight:Float):Void {
        layoutRoot.style.width = LayoutAxis.fixed(logicalWidth);
        layoutRoot.style.height = LayoutAxis.fixed(logicalHeight);
        layoutRoot.style.padding = new Insets(14.0, 14.0, 0.0, 0.0);
        layoutLabel.text = animate ? "LAYOUT  ·  ANIMATE ON" : "LAYOUT  ·  ANIMATE OFF";
        layoutFrame.width = logicalWidth;
        layoutFrame.height = logicalHeight;
        layoutFrame.setPointer(pointerX, pointerY, pointerDown || pointerPressedPending);
        layoutFrame.deltaSeconds = 1.0 / TARGET_FPS;
        var events = layoutSession.submit(layoutRoot, layoutFrame);
        pointerPressedPending = false;
        for (event in events)
            if (event.nodeId == layoutButton.id && event.isButtonActivated())
                animate = !animate;
    }

    function updateCaret():Void {
        if (pointerX < 252.0 || pointerX > 868.0 || pointerY < 392.0 || pointerY > 510.0)
            return;
        caretPosition = multilingual.hitTest(pointerX - TEXT_ORIGIN_X, pointerY - TEXT_ORIGIN_Y);
    }

    /** Encodes one complete frame using only the typed Haxe graphics API. */
    public function encodeFrame(seconds:Float, logicalWidth:Float, logicalHeight:Float,
            framebufferWidth:Int, framebufferHeight:Int, pixelScale:Float,
            staticFrame:Bool):Void {
        setViewport(logicalWidth, logicalHeight);
        submitLayoutFrame(logicalWidth, logicalHeight);
        var replacedCaret:Null<Path> = null;
        var sizeChanged = framebufferWidth != statusFramebufferWidth ||
            framebufferHeight != statusFramebufferHeight || pixelScale != statusPixelScale;
        var refreshStatus = statusLayout == null || sizeChanged || staticFrame ||
            statusLastRefresh < 0.0 || seconds - statusLastRefresh >= STATUS_REFRESH_SECONDS;
        if (refreshStatus) {
            var status = 'OPENGL · retained display list · typed compositor\n' +
                'vector paths · paints · text · image';
            if (statusText == null || status != statusText) {
                if (statusLayout == null)
                    statusLayout = styled(status, 240.0, 10.0);
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
            // Skribidi reports caret.x at the baseline and a slope in
            // dx/dy form. Keep the caret in the same layout coordinate space
            // as drawing and hit testing, including italic fonts.
            var caretX1 = TEXT_ORIGIN_X + caret.x + caret.slope * caret.ascender;
            var caretY1 = TEXT_ORIGIN_Y + caret.y + caret.ascender;
            var caretX2 = TEXT_ORIGIN_X + caret.x + caret.slope * caret.descender;
            var caretY2 = TEXT_ORIGIN_Y + caret.y + caret.descender;
            caretPath = caretX1 != caretX2 || caretY1 != caretY2
                ? linePath(caretX1, caretY1, caretX2, caretY2)
                : null;
            caretPathOffset = caretPosition.offset;
            caretPathAffinity = caretPosition.affinity;
        }
        if (caretStatusLayout == null || caretStatusOffset != caretPosition.offset ||
            caretStatusAffinity != caretPosition.affinity) {
            var caretStatus = 'offset ${caretPosition.offset} · affinity ${caretPosition.affinity}';
            if (caretStatusLayout == null)
                caretStatusLayout = styled(caretStatus, 570.0, 10.0);
            else
                caretStatusLayout.setText(caretStatus);
            caretStatusOffset = caretPosition.offset;
            caretStatusAffinity = caretPosition.affinity;
        }

        canvas.reset();
        var base:Paint = lightTheme ? lightBackground : background;
        canvas.save();
        canvas.translate(sceneOffsetX, sceneOffsetY);
        canvas.scale(sceneScale, sceneScale);
        canvas.fill(backgroundPath, base);

        canvas.save();
        canvas.clip(new Rect(0.0, 0.0, 220.0, LOGICAL_HEIGHT));
        canvas.fill(sidebarPath, sidebar);
        canvas.setAlpha(0.9);
        canvas.save();
        canvas.translate(28.0, 42.0);
        canvas.fill(dotPath, green);
        canvas.restore();
        canvas.save();
        canvas.translate(24.0, 68.0);
        canvas.drawText(title, 0.0, 0.0);
        canvas.restore();
        canvas.drawText(sidebarCopy, 24.0, 134.0);
        canvas.drawText(controlsLabel, 24.0, 310.0);
        canvas.fill(animationButtonPath, animate ? green : cardRaised);
        canvas.drawText(animate ? animateOnLabel : animateOffLabel, 40.0, 375.0);
        canvas.fill(sliderTrackPath, cardRaised);
        canvas.save();
        canvas.translate(24.0, 424.0);
        canvas.scale(172.0 * layerOpacity, 6.0);
        canvas.fill(unitRectPath, cyan);
        canvas.restore();
        canvas.save();
        canvas.translate(24.0 + 172.0 * layerOpacity, 427.0);
        canvas.fill(dotPath, ink);
        canvas.restore();
        canvas.fill(themeButtonPath, lightTheme ? orange : cardRaised);
        canvas.drawText(lightTheme ? lightThemeLabel : darkThemeLabel, 40.0, 497.0);
        canvas.restore();

        canvas.fill(headerPath, cardRaised);
        canvas.drawText(title, HEADER_TITLE_X, HEADER_TITLE_Y);
        canvas.drawText(subtitle, HEADER_SUBTITLE_X, HEADER_SUBTITLE_Y);
        canvas.drawText(statusLayout, HEADER_STATUS_X, HEADER_STATUS_Y);

        canvas.fill(vectorCardPath, card);
        canvas.drawText(vectorLabel, 262.0, 132.0);
        canvas.save();
        canvas.translate(320.0, 218.0);
        canvas.rotate(staticFrame ? 0.0 : seconds * 0.45);
        canvas.fill(starPath, violet);
        canvas.stroke(starPath, ink, 2.0, LineCap.Round, LineJoin.Round);
        canvas.restore();
        canvas.save();
        canvas.translate(420.0, 218.0);
        canvas.fill(donutPath, cyan);
        canvas.stroke(donutPath, ink, 1.5, LineCap.Butt, LineJoin.Bevel);
        canvas.restore();
        canvas.save();
        canvas.translate(500.0, 218.0);
        canvas.rotate(staticFrame ? 0.0 : seconds * -0.6);
        canvas.stroke(curvePath, orange, 4.0, LineCap.Round, LineJoin.Miter);
        canvas.restore();
        canvas.drawText(curveLegendLabel, 264.0, 318.0);

        canvas.fill(paintCardPath, card);
        canvas.drawText(paintLabel, 582.0, 132.0);
        canvas.save();
        canvas.translate(610.0, 186.0);
        canvas.fill(circle22Path, green);
        canvas.translate(60.0, 0.0);
        canvas.fill(circle22Path, violet);
        canvas.translate(60.0, 0.0);
        canvas.fill(circle22Path, orange);
        canvas.restore();
        canvas.save();
        canvas.clip(new Rect(594.0, 224.0, 252.0, 88.0));
        canvas.beginLayer(layerOpacity);
        canvas.drawImage(image, new Rect(600.0, 230.0, 84.0, 84.0));
        canvas.save();
        canvas.translate(790.0, 270.0);
        canvas.fill(diamondPath, cyan);
        canvas.restore();
        canvas.endLayer();
        canvas.restore();
        canvas.drawText(paintCaption, 582.0, 330.0);

        canvas.fill(textCardPath, cardRaised);
        canvas.drawText(textLabel, 262.0, 388.0);
        canvas.drawText(multilingual, TEXT_ORIGIN_X, TEXT_ORIGIN_Y);
        canvas.drawText(caretInstruction, 260.0, 464.0);
        if (caretPath != null)
            canvas.stroke(caretPath, green, 2.0, LineCap.Round, LineJoin.Round);
        canvas.drawText(caretStatusLayout, 260.0, 495.0);

        canvas.fill(retainedCardPath, card);
        canvas.drawText(retainedLabel, 262.0, 559.0);
        canvas.save();
        canvas.clip(new Rect(252.0, 568.0, 290.0, 54.0));
        for (index in 0...18) {
            canvas.save();
            canvas.translate(265.0 + (index % 9) * 31.0,
                594.0 + (index < 9 ? 0.0 : 16.0));
            canvas.setAlpha(0.25 + (index % 5) * 0.14);
            canvas.fill(starPath, index % 2 == 0 ? green : cyan);
            canvas.restore();
        }
        canvas.restore();

        canvas.fill(surfaceCardPath, card);
        canvas.drawText(surfaceLabel, 582.0, 559.0);
        canvas.drawText(offscreenLabel, 582.0, 586.0);
        canvas.drawText(targetLabel, 582.0, 604.0);
        canvas.beginLayer(0.75);
        canvas.save();
        canvas.translate(836.0, 583.0);
        canvas.fill(diamondPath, rose);
        canvas.restore();
        canvas.endLayer();
        canvas.drawText(footerLabel, 262.0, 643.0);
        canvas.restore();
        canvas.update(list);
        if (replacedCaret != null)
            replacedCaret.dispose();
    }

    public function render(surface:Int, logicalWidth:Float, logicalHeight:Float,
            framebufferWidth:Int, framebufferHeight:Int, pixelScale:Float):Void {
        frameInfo.set(logicalWidth, logicalHeight, framebufferWidth, framebufferHeight,
            pixelScale);
        var target = Surface.fromNativeHandle(surface);
        renderer.renderFrame(list, target, frameInfo);
        layoutSession.renderOverlay(renderer, target, frameInfo);
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
        layoutSession.dispose();
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
                var cellX = Std.int(x * IMAGE_TEXTURE_CELLS / width);
                var cellY = Std.int(y * IMAGE_TEXTURE_CELLS / height);
                var bright = ((cellX + cellY) % 2) == 0;
                var offset = (y * width + x) * 4;
                pixels.set(offset, bright ? 49 : 18);
                pixels.set(offset + 1, bright ? 202 : 76);
                pixels.set(offset + 2, bright ? 239 : 161);
                pixels.set(offset + 3, 255);
            }
        return Image.create(width, height, ImageFormat.RGBA8, pixels);
    }

}
