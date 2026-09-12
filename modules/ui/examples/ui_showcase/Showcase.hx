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
    static inline var IMAGE_TEXTURE_SIZE:Int = 256;
    static inline var IMAGE_TEXTURE_CELLS:Int = 6;
    public static inline var TARGET_FPS:Float = 60.0;
    public static inline var STATUS_REFRESH_SECONDS:Float = 0.25;

    final list:DisplayList;
    final renderer:Renderer;
    final fonts:FontCollection;
    final layoutSession:LayoutSession;
    final layoutRoot:LayoutNode;
    final layoutSidebar:LayoutNode;
    final layoutSidebarSpacer:LayoutNode;
    final layoutMain:LayoutNode;
    final layoutHeader:LayoutNode;
    final layoutTopRow:LayoutNode;
    final layoutVectorCard:LayoutNode;
    final layoutPaintCard:LayoutNode;
    final layoutTextCard:LayoutNode;
    final layoutBottomRow:LayoutNode;
    final layoutRetainedCard:LayoutNode;
    final layoutSurfaceCard:LayoutNode;
    final layoutFooter:LayoutNode;
    final layoutButton:LayoutNode;
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
    final image:Image;
    final cubeSurface:GraphicsSurface;

    final title:TextLayout;
    final sidebarTitle:TextLayout;
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
    final benchmarkBaseText:String;

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
    var caretPathOriginX:Float = -1.0;
    var caretPathOriginY:Float = -1.0;
    var caretStatusOffset:Int = -1;
    var caretStatusAffinity:Int = -1;
    var caretPosition:TextPosition;
    var pointerX:Float = 0.0;
    var pointerY:Float = 0.0;
    var pointerDown:Bool = false;
    var pointerPressedPending:Bool = false;
    var layerOpacity:Float = 0.68;
    var animate:Bool = true;
    var cubeRotation:Float = 0.65;
    var previousAnimationTime:Float = 0.0;
    var lightTheme:Bool = false;
    var sidebarBounds:Rect;
    var buttonBounds:Rect;
    var headerBounds:Rect;
    var vectorBounds:Rect;
    var paintBounds:Rect;
    var textBounds:Rect;
    var retainedBounds:Rect;
    var surfaceBounds:Rect;
    var footerBounds:Rect;

    /** Takes ownership of the configured font collection. */
    function new(fonts:FontCollection, ?multilingualText:String) {
        benchmarkBaseText = multilingualText == null ? "NativeKit — مرحبا — שלום — こんにちは 👋" : multilingualText;
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
        layoutSidebar = LayoutNode.box(9004);
        layoutSidebarSpacer = LayoutNode.box(9005);
        layoutMain = LayoutNode.box(9006);
        layoutHeader = LayoutNode.box(9007);
        layoutTopRow = LayoutNode.box(9008);
        layoutVectorCard = LayoutNode.box(9009);
        layoutPaintCard = LayoutNode.box(9010);
        layoutTextCard = LayoutNode.box(9011);
        layoutBottomRow = LayoutNode.box(9012);
        layoutRetainedCard = LayoutNode.box(9013);
        layoutSurfaceCard = LayoutNode.box(9014);
        layoutFooter = LayoutNode.box(9015);
        layoutButton = LayoutNode.button(9002);
        layoutRoot.style.direction = LayoutDirection.LeftToRight;
        layoutSidebar.style.width = LayoutAxis.fixed(220.0);
        layoutSidebar.style.height = LayoutAxis.grow();
        layoutSidebar.style.padding = new Insets(24.0, 0.0, 24.0, 0.0);
        layoutSidebarSpacer.style.width = LayoutAxis.grow();
        layoutSidebarSpacer.style.height = LayoutAxis.fixed(326.0);
        layoutButton.style.width = LayoutAxis.grow();
        layoutButton.style.height = LayoutAxis.fixed(40.0);
        layoutSidebar.add(layoutSidebarSpacer);
        layoutSidebar.add(layoutButton);
        layoutMain.style.width = LayoutAxis.grow();
        layoutMain.style.height = LayoutAxis.grow();
        layoutMain.style.padding = new Insets(24.0, 12.0, 24.0, 0.0);
        layoutMain.style.childGap = 12.0;
        layoutHeader.style.width = LayoutAxis.grow();
        layoutHeader.style.height = LayoutAxis.fixed(76.0);
        layoutTopRow.style.width = LayoutAxis.grow();
        layoutTopRow.style.height = LayoutAxis.fixed(236.0);
        layoutTopRow.style.direction = LayoutDirection.LeftToRight;
        layoutTopRow.style.childGap = 14.0;
        layoutVectorCard.style.width = LayoutAxis.grow();
        layoutVectorCard.style.height = LayoutAxis.grow();
        layoutPaintCard.style.width = LayoutAxis.grow();
        layoutPaintCard.style.height = LayoutAxis.grow();
        layoutTextCard.style.width = LayoutAxis.grow();
        layoutTextCard.style.height = LayoutAxis.fixed(154.0);
        layoutBottomRow.style.width = LayoutAxis.grow();
        layoutBottomRow.style.height = LayoutAxis.fixed(90.0);
        layoutBottomRow.style.direction = LayoutDirection.LeftToRight;
        layoutBottomRow.style.childGap = 14.0;
        layoutRetainedCard.style.width = LayoutAxis.grow();
        layoutRetainedCard.style.height = LayoutAxis.grow();
        layoutSurfaceCard.style.width = LayoutAxis.grow();
        layoutSurfaceCard.style.height = LayoutAxis.grow();
        layoutFooter.style.width = LayoutAxis.grow();
        layoutFooter.style.height = LayoutAxis.grow();
        layoutTopRow.add(layoutVectorCard);
        layoutTopRow.add(layoutPaintCard);
        layoutBottomRow.add(layoutRetainedCard);
        layoutBottomRow.add(layoutSurfaceCard);
        layoutMain.add(layoutHeader);
        layoutMain.add(layoutTopRow);
        layoutMain.add(layoutTextCard);
        layoutMain.add(layoutBottomRow);
        layoutMain.add(layoutFooter);
        layoutRoot.add(layoutSidebar);
        layoutRoot.add(layoutMain);

        sidebarBounds = new Rect(0.0, 0.0, 220.0, LOGICAL_HEIGHT);
        buttonBounds = new Rect(24.0, 350.0, 172.0, 40.0);
        headerBounds = new Rect(244.0, 20.0, 632.0, 76.0);
        vectorBounds = new Rect(244.0, 108.0, 306.0, 236.0);
        paintBounds = new Rect(564.0, 108.0, 312.0, 236.0);
        textBounds = new Rect(244.0, 356.0, 632.0, 154.0);
        retainedBounds = new Rect(244.0, 522.0, 306.0, 90.0);
        surfaceBounds = new Rect(564.0, 522.0, 312.0, 90.0);
        footerBounds = new Rect(244.0, 624.0, 632.0, 26.0);

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
        // Keep enough source resolution for the image panel at high-DPI scales.
        // The renderer remains linear-filtered; this avoids enlarging a tiny
        // diagnostic bitmap until its source texels become visible.
        image = keep(checkerImage(IMAGE_TEXTURE_SIZE, IMAGE_TEXTURE_SIZE));
        cubeSurface = keep(GraphicsSurface.createShowcaseCube());

        title = styled("NativeKit Graphics Lab", 600.0, 29.0);
        sidebarTitle = styled("NativeKit", 170.0, 27.0);
        subtitle = styled("Typed Haxe graphics · retained display list · one compositor", 600.0, 14.0);
        vectorLabel = styled("VECTOR GRAPHICS", 270.0, 12.0);
        paintLabel = styled("PAINT + IMAGE", 270.0, 12.0);
        textLabel = styled("UNICODE TEXT + CARET", 580.0, 12.0);
        retainedLabel = styled("RETAINED PATH", 260.0, 12.0);
        surfaceLabel = styled("GRAPHICS SURFACE", 270.0, 12.0);
        multilingual = styled(benchmarkBaseText, 580.0, 18.0);
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
        offscreenLabel = styled("realtime indexed cube", 190.0, 10.0);
        targetLabel = styled("depth tested · shared compositor", 190.0, 10.0);

        caretPosition = multilingual.hitTest(160.0, 22.0);
    }

    /** Updates the responsive layout viewport. */
    public function setViewport(width:Float, height:Float):Void {
        if (width <= 0.0 || height <= 0.0)
            return;
    }

    function keep<T:NativeKitUIResource>(resource:T):T {
        resources.push(resource);
        return resource;
    }

    function styled(value:String, width:Float, fontSize:Float):TextLayout
        return keep(TextLayout.createStyled(fonts, value, width, new TextStyle(fontSize),
            new ParagraphStyle()));

    public function updatePointer(x:Float, y:Float):Void {
        pointerX = x;
        pointerY = y;
        updateCaret();
    }

    /** Benchmark-only controls keep repeatable workloads out of browser input timing. */
    public function setBenchmarkAnimation(enabled:Bool):Void
        animate = enabled;

    public function benchmarkTextEdit(step:Int):Void {
        multilingual.setText(benchmarkBaseText + " · " + step);
        pointerX = textOriginX() + 18.0 + (step % 36) * 14.0;
        pointerY = textOriginY() + 5.0;
        updateCaret();
    }

    public function pointerButton(x:Float, y:Float, pressed:Bool):Void {
        updatePointer(x, y);
        pointerDown = pressed;
        if (pressed)
            pointerPressedPending = true;
        if (!pressed)
            return;
        if (contains(buttonBounds, pointerX, pointerY)) {
            animate = !animate;
        } else if (pointerX >= sidebarBounds.x + 24.0 &&
                pointerX <= sidebarBounds.x + sidebarBounds.width - 24.0 &&
                pointerY >= buttonBounds.y + 64.0 && pointerY <= buttonBounds.y + 104.0) {
            layerOpacity = Math.max(0.15, Math.min(1.0,
                (pointerX - sidebarBounds.x - 24.0) / (sidebarBounds.width - 48.0)));
        } else if (pointerX >= sidebarBounds.x + 24.0 &&
                pointerX <= sidebarBounds.x + sidebarBounds.width - 24.0 &&
                pointerY >= buttonBounds.y + 112.0 && pointerY <= buttonBounds.y + 154.0) {
            lightTheme = !lightTheme;
        }
        updateCaret();
    }

    public inline function caretOffset():Int
        return caretPosition.offset;

    public inline function caretAffinity():Int
        return caretPosition.affinity;

    public function caretDirection():Int
        return multilingual.caret(caretPosition).direction;

    function submitLayoutFrame(logicalWidth:Float, logicalHeight:Float):Void {
        layoutRoot.style.width = LayoutAxis.fixed(logicalWidth);
        layoutRoot.style.height = LayoutAxis.fixed(logicalHeight);
        var compact = logicalWidth < 760.0;
        layoutSidebar.style.width = LayoutAxis.fixed(compact ? 184.0 : 220.0);
        layoutMain.style.padding = new Insets(compact ? 12.0 : 24.0, 12.0,
            compact ? 12.0 : 24.0, 0.0);
        layoutFrame.width = logicalWidth;
        layoutFrame.height = logicalHeight;
        layoutFrame.setPointer(pointerX, pointerY, pointerDown || pointerPressedPending);
        layoutFrame.deltaSeconds = 1.0 / TARGET_FPS;
        var events = layoutSession.submit(layoutRoot, layoutFrame);
        pointerPressedPending = false;
        for (event in events)
            if (event.nodeId == layoutButton.id && event.isButtonActivated())
                animate = !animate;
        sidebarBounds = layoutSession.item(layoutSidebar);
        buttonBounds = layoutSession.item(layoutButton);
        headerBounds = layoutSession.item(layoutHeader);
        vectorBounds = layoutSession.item(layoutVectorCard);
        paintBounds = layoutSession.item(layoutPaintCard);
        textBounds = layoutSession.item(layoutTextCard);
        retainedBounds = layoutSession.item(layoutRetainedCard);
        surfaceBounds = layoutSession.item(layoutSurfaceCard);
        footerBounds = layoutSession.item(layoutFooter);
    }

    static function contains(rect:Rect, x:Float, y:Float):Bool
        return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;

    function updateCaret():Void {
        if (!contains(textBounds, pointerX, pointerY))
            return;
        caretPosition = multilingual.hitTest(pointerX - textOriginX(), pointerY - textOriginY());
    }

    inline function textOriginX():Float
        return textBounds.x + 16.0;

    inline function textOriginY():Float
        return textBounds.y + 61.0;

    /** Encodes one complete frame using only the typed Haxe graphics API. */
    public function encodeFrame(seconds:Float, logicalWidth:Float, logicalHeight:Float,
            framebufferWidth:Int, framebufferHeight:Int, pixelScale:Float,
            staticFrame:Bool):Void {
        if (staticFrame)
            cubeRotation = 0.65;
        else if (animate)
            cubeRotation += Math.max(0.0, seconds - previousAnimationTime) * 0.8;
        previousAnimationTime = seconds;
        cubeSurface.setShowcaseCubeRotation(cubeRotation);
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
            caretPathAffinity != caretPosition.affinity || caretPathOriginX != textOriginX() ||
            caretPathOriginY != textOriginY();
        if (caretChanged) {
            var caret = multilingual.caret(caretPosition);
            replacedCaret = caretPath;
            // Skribidi reports caret.x at the baseline and a slope in
            // dx/dy form. Keep the caret in the same layout coordinate space
            // as drawing and hit testing, including italic fonts.
            var caretX1 = textOriginX() + caret.x + caret.slope * caret.ascender;
            var caretY1 = textOriginY() + caret.y + caret.ascender;
            var caretX2 = textOriginX() + caret.x + caret.slope * caret.descender;
            var caretY2 = textOriginY() + caret.y + caret.descender;
            caretPath = caretX1 != caretX2 || caretY1 != caretY2
                ? linePath(caretX1, caretY1, caretX2, caretY2)
                : null;
            caretPathOffset = caretPosition.offset;
            caretPathAffinity = caretPosition.affinity;
            caretPathOriginX = textOriginX();
            caretPathOriginY = textOriginY();
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
        fillRect(canvas, new Rect(0.0, 0.0, logicalWidth, logicalHeight), base);

        canvas.save();
        canvas.clip(sidebarBounds);
        fillRect(canvas, sidebarBounds, sidebar);
        canvas.setAlpha(0.9);
        canvas.save();
        canvas.translate(28.0, 42.0);
        canvas.fill(dotPath, green);
        canvas.restore();
        canvas.save();
        canvas.translate(24.0, 68.0);
        canvas.drawText(sidebarTitle, 0.0, 0.0);
        canvas.restore();
        canvas.drawText(sidebarCopy, 24.0, 134.0);
        canvas.drawText(controlsLabel, 24.0, 310.0);
        fillRect(canvas, buttonBounds, animate ? green : cardRaised);
        canvas.drawText(animate ? animateOnLabel : animateOffLabel, buttonBounds.x + 16.0,
            buttonBounds.y + 25.0);
        var sliderX = sidebarBounds.x + 24.0;
        var sliderWidth = sidebarBounds.width - 48.0;
        var sliderY = buttonBounds.y + 74.0;
        fillRect(canvas, new Rect(sliderX, sliderY, sliderWidth, 6.0), cardRaised);
        canvas.save();
        canvas.translate(sliderX, sliderY);
        canvas.scale(sliderWidth * layerOpacity, 6.0);
        canvas.fill(unitRectPath, cyan);
        canvas.restore();
        canvas.save();
        canvas.translate(sliderX + sliderWidth * layerOpacity, sliderY + 3.0);
        canvas.fill(dotPath, ink);
        canvas.restore();
        var themeBounds = new Rect(sliderX, sliderY + 48.0, sliderWidth, 40.0);
        fillRect(canvas, themeBounds, lightTheme ? orange : cardRaised);
        canvas.drawText(lightTheme ? lightThemeLabel : darkThemeLabel, themeBounds.x + 16.0,
            themeBounds.y + 25.0);
        canvas.restore();

        fillMapped(canvas, headerPath, HEADER_X, HEADER_Y, HEADER_WIDTH, HEADER_HEIGHT,
            headerBounds, cardRaised);
        canvas.drawText(title, headerBounds.x + 26.0, headerBounds.y + 26.0);
        canvas.drawText(subtitle, headerBounds.x + 28.0, headerBounds.y + 54.0);
        if (headerBounds.width >= 540.0)
            canvas.drawText(statusLayout, headerBounds.x + headerBounds.width - 254.0,
                headerBounds.y + 14.0);

        fillMapped(canvas, vectorCardPath, 244.0, 108.0, 306.0, 236.0, vectorBounds, card);
        canvas.drawText(vectorLabel, vectorBounds.x + 18.0, vectorBounds.y + 24.0);
        canvas.save();
        canvas.translate(vectorBounds.x + vectorBounds.width * 0.25,
            vectorBounds.y + vectorBounds.height * 0.47);
        canvas.rotate(staticFrame ? 0.0 : seconds * 0.45);
        canvas.fill(starPath, violet);
        canvas.stroke(starPath, ink, 2.0, LineCap.Round, LineJoin.Round);
        canvas.restore();
        canvas.save();
        canvas.translate(vectorBounds.x + vectorBounds.width * 0.58,
            vectorBounds.y + vectorBounds.height * 0.47);
        canvas.fill(donutPath, cyan);
        canvas.stroke(donutPath, ink, 1.5, LineCap.Butt, LineJoin.Bevel);
        canvas.restore();
        canvas.save();
        canvas.translate(vectorBounds.x + vectorBounds.width * 0.84,
            vectorBounds.y + vectorBounds.height * 0.47);
        canvas.rotate(staticFrame ? 0.0 : seconds * -0.6);
        canvas.stroke(curvePath, orange, 4.0, LineCap.Round, LineJoin.Miter);
        canvas.restore();
        canvas.drawText(curveLegendLabel, vectorBounds.x + 20.0,
            vectorBounds.y + vectorBounds.height - 26.0);

        fillMapped(canvas, paintCardPath, 564.0, 108.0, 312.0, 236.0, paintBounds, card);
        canvas.drawText(paintLabel, paintBounds.x + 18.0, paintBounds.y + 24.0);
        canvas.save();
        canvas.translate(paintBounds.x + 46.0, paintBounds.y + 78.0);
        canvas.fill(circle22Path, green);
        canvas.translate(60.0, 0.0);
        canvas.fill(circle22Path, violet);
        canvas.translate(60.0, 0.0);
        canvas.fill(circle22Path, orange);
        canvas.restore();
        canvas.save();
        var imageClip = new Rect(paintBounds.x + 30.0, paintBounds.y + 116.0,
            paintBounds.width - 60.0, 88.0);
        canvas.clip(imageClip);
        canvas.beginLayer(layerOpacity);
        canvas.drawImage(image, new Rect(imageClip.x + 6.0, imageClip.y + 6.0, 84.0, 84.0));
        canvas.save();
        canvas.translate(imageClip.x + imageClip.width - 56.0, imageClip.y + 46.0);
        canvas.fill(diamondPath, cyan);
        canvas.restore();
        canvas.endLayer();
        canvas.restore();
        canvas.drawText(paintCaption, paintBounds.x + 18.0,
            paintBounds.y + paintBounds.height - 14.0);

        fillMapped(canvas, textCardPath, 244.0, 364.0, 632.0, 154.0, textBounds, cardRaised);
        canvas.drawText(textLabel, textBounds.x + 18.0, textBounds.y + 24.0);
        canvas.drawText(multilingual, textOriginX(), textOriginY());
        canvas.drawText(caretInstruction, textBounds.x + 16.0, textBounds.y + 100.0);
        if (caretPath != null)
            canvas.stroke(caretPath, green, 2.0, LineCap.Round, LineJoin.Round);
        canvas.drawText(caretStatusLayout, textBounds.x + 16.0, textBounds.y + 131.0);

        fillMapped(canvas, retainedCardPath, 244.0, 538.0, 306.0, 90.0, retainedBounds, card);
        canvas.drawText(retainedLabel, retainedBounds.x + 18.0, retainedBounds.y + 21.0);
        canvas.save();
        canvas.clip(new Rect(retainedBounds.x + 8.0, retainedBounds.y + 30.0,
            retainedBounds.width - 16.0, retainedBounds.height - 36.0));
        for (index in 0...18) {
            canvas.save();
            canvas.translate(retainedBounds.x + 21.0 + (index % 9) *
                ((retainedBounds.width - 42.0) / 8.0),
                retainedBounds.y + 56.0 + (index < 9 ? 0.0 : 16.0));
            canvas.setAlpha(0.25 + (index % 5) * 0.14);
            canvas.fill(starPath, index % 2 == 0 ? green : cyan);
            canvas.restore();
        }
        canvas.restore();

        fillMapped(canvas, surfaceCardPath, 564.0, 538.0, 312.0, 90.0, surfaceBounds, card);
        var cubeRect = new Rect(surfaceBounds.x + surfaceBounds.width - 88.0,
            surfaceBounds.y + 5.0, 80.0, 80.0);
        canvas.withClip(surfaceBounds, function(c) {
            c.withLayer(layerOpacity, function(c) {
                c.drawSurface(cubeSurface, cubeRect);
            });
        });
        canvas.drawText(surfaceLabel, surfaceBounds.x + 18.0, surfaceBounds.y + 21.0);
        canvas.drawText(offscreenLabel, surfaceBounds.x + 18.0, surfaceBounds.y + 48.0);
        canvas.drawText(targetLabel, surfaceBounds.x + 18.0, surfaceBounds.y + 66.0);
        canvas.drawText(footerLabel, footerBounds.x + 18.0, footerBounds.y + 19.0);
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

    function fillRect(canvas:Canvas, bounds:Rect, paint:Paint):Void {
        canvas.save();
        canvas.translate(bounds.x, bounds.y);
        canvas.scale(bounds.width, bounds.height);
        canvas.fill(unitRectPath, paint);
        canvas.restore();
    }

    static function fillMapped(canvas:Canvas, path:Path, sourceX:Float, sourceY:Float,
            sourceWidth:Float, sourceHeight:Float, bounds:Rect, paint:Paint):Void {
        canvas.save();
        canvas.translate(bounds.x, bounds.y);
        canvas.scale(bounds.width / sourceWidth, bounds.height / sourceHeight);
        canvas.translate(-sourceX, -sourceY);
        canvas.fill(path, paint);
        canvas.restore();
    }

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
