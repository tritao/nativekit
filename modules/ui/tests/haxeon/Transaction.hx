import NativeKit;
import NativeKit.EventKind;
import NativeKit.GraphicsApi;
import NativeKit.Result;
import NativeKit.Event;
import NativeKit.InitOptions;
import NativeKit.SurfaceOptions;
import NativeKit.WindowOptions;
import haxe.io.Bytes;

class Transaction {
	static function main():Int {
		var init = new InitOptions();
		init.set_struct_size(InitOptions.size());
		init.set_api_version(NativeKitConstants.NK_API_VERSION);
		init.set_event_queue_capacity(32);
		if (NativeKit.nk_init(init) != Result.Ok)
			return 10;

		var windowOptions = new WindowOptions();
		windowOptions.set_struct_size(WindowOptions.size());
		windowOptions.set_flags(NativeKitConstants.NK_WINDOW_RESIZABLE);
		windowOptions.set_width(256);
		windowOptions.set_height(192);
		windowOptions.set_title("Haxeon: NativeKit UI");
		var createdWindow = NativeKit.nk_window_create(windowOptions);
		if (createdWindow.status != Result.Ok) {
			NativeKit.nk_shutdown();
			return 11;
		}
		var window = createdWindow.out_window;
		var surfaceOptions = new SurfaceOptions();
		surfaceOptions.set_struct_size(SurfaceOptions.size());
		surfaceOptions.set_flags(NativeKitConstants.NK_SURFACE_FORWARD_COMPATIBLE |
			NativeKitConstants.NK_SURFACE_STENCIL);
		surfaceOptions.set_api(GraphicsApi.Opengl);
		surfaceOptions.set_major_version(3);
		surfaceOptions.set_minor_version(3);
		surfaceOptions.set_width(256);
		surfaceOptions.set_height(192);
		var createdSurface = NativeKit.nk_surface_create(window, surfaceOptions);
		if (createdSurface.status != Result.Ok) {
			NativeKit.nk_window_destroy(window);
			NativeKit.nk_shutdown();
			return 12;
		}
		var surface = createdSurface.out_surface;

		var list = DisplayList.create();
		var path = new PathBuilder().moveTo(0.0, 0.0).lineTo(244.0, 12.0).lineTo(244.0, 180.0).lineTo(12.0, 180.0).close().build();
		var paint = SolidPaint.create(Color.rgba(0.2, 0.6, 0.9));
		var pixels = Bytes.alloc(4);
		pixels.set(0, 20); pixels.set(1, 80); pixels.set(2, 220); pixels.set(3, 255);
		var image = Image.create(1, 1, ImageFormat.RGBA8, pixels);
		var fonts = FontCollection.create();
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null)
			return 9;
		fonts.add(fontPath);
		var textStyle = new TextStyle(18.0);
		textStyle.letterSpacing = -0.25;
		var paragraphStyle = new ParagraphStyle();
		paragraphStyle.wrap = TextWrap.Word;
		var text = TextLayout.createStyled(fonts, "NativeKit — こんにちは — مرحبا", 220.0,
			textStyle, paragraphStyle);
		textStyle.fontSize = 30.0;
		paragraphStyle.wrap = TextWrap.None;
		if (text.textStyle.fontSize != 18.0 || text.textStyle.letterSpacing != -0.25 ||
			text.paragraphStyle.wrap != TextWrap.Word)
			return 18;
		var updatedTextStyle = new TextStyle(19.5, FontFamily.Default, -0.5);
		var updatedParagraphStyle = new ParagraphStyle(TextWrap.WordCharacter,
			TextAlignment.Center, 22.0, TextDirection.Rtl);
		text.update("Updated — こんにちは — مرحبا", 220.0, updatedTextStyle,
			updatedParagraphStyle);
		updatedTextStyle.fontSize = 28.0;
		updatedParagraphStyle.alignment = TextAlignment.End;
		if (text.textStyle.fontSize != 19.5 || text.textStyle.letterSpacing != -0.5 ||
			text.paragraphStyle.alignment != TextAlignment.Center ||
			text.paragraphStyle.lineHeight != 22.0 ||
			text.paragraphStyle.direction != TextDirection.Rtl || textStyle.fontSize != 30.0 ||
			paragraphStyle.wrap != TextWrap.None)
			return 19;
		var metrics = text.measure();
		var position = text.hitTest(16.0, 24.0);
		var caret = text.caret(position);
		if (metrics.width <= 0.0 || position.offset < 0 || caret.slope != caret.slope)
			return 8;
		var canvas = new Canvas(8);
		canvas.withState(function(canvas) {
			canvas.translate(4.0, 5.0);
			canvas.withClip(new Rect(0.0, 0.0, 100.0, 80.0), function(canvas) {
				canvas.setAlpha(0.5);
				canvas.fill(path, paint);
				canvas.stroke(path, paint, 2.0, LineCap.Round, LineJoin.Round);
				canvas.drawText(text, 16.0, 24.0);
				canvas.withLayer(0.6, function(canvas) {
					canvas.drawImage(image, new Rect(4.0, 4.0, 12.0, 12.0));
				});
			});
		});
		canvas.update(list);
		var info = list.info();
		if (info.commandCount != 15 || info.commandBytes <= 0)
			return 3;

		var renderer = Renderer.create();
		var ready = false;
		var rendered = 0;
		var attempts = 0;
		while (rendered < 3 && attempts < 120) {
			var event = new Event();
			var eventBytes:Bytes = event;
			for (index in 0...Event.size()) eventBytes.set(index, 0);
			event.set_struct_size(Event.size());
			var polled = NativeKit.nk_poll_event(event);
			if (polled.status != Result.Ok)
				return 14;
			var eventKind = polled.event.get_kind();
			var eventSource = polled.event.get_source();
			NativeKit.nk_event_release(polled.event);
			if (eventKind == EventKind.SurfaceReady && eventSource == surface)
				ready = true;
			if (ready) {
				var size = NativeKit.nk_surface_get_framebuffer_size(surface);
				if (size.status != Result.Ok || size.out_width <= 0 ||
					size.out_height <= 0)
					return 14;
				var scale = NativeKit.nk_window_get_scale(window);
				if (scale.status != Result.Ok || scale.out_scale <= 0.0)
					return 15;
				var frame = new FrameInfo(256.0, 192.0, size.out_width, size.out_height, scale.out_scale);
				try {
					renderer.renderFrame(list, Surface.fromNativeHandle(surface), frame);
				} catch (_:Dynamic) {
					return 16;
				}
				if (NativeKit.nk_surface_present(surface) != Result.Ok)
					return 16;
				rendered++;
			}
			attempts++;
		}
		if (!ready || rendered != 3)
			return 17;
		var stats = renderer.stats();
		renderer.dispose();
		list.clear();
		info = list.info();
		if (info.commandCount != 0)
			return 5;
		canvas.reset();
		canvas.update(list);
		if (list.info().commandCount != 0)
			return 6;
		list.dispose();
		text.dispose();
		fonts.dispose();
		image.dispose();
		paint.dispose();
		path.dispose();
		var staleRejected = false;
		try {
			canvas.setPaint(paint);
		} catch (_:Dynamic) {
			staleRejected = true;
		}
		if (!staleRejected)
			return 7;
		NativeKit.nk_surface_destroy(surface);
		NativeKit.nk_window_destroy(window);
		NativeKit.nk_shutdown();
		return 0;
	}
}
