import NativeKit;
import NativeKitUI;
import NativeKitUI.Nkui_image_format;
import NativeKitUI.Nkui_path_verb;
import NativeKitUI.Nkui_result;
import haxe.io.Bytes;

class Transaction {
	static function main():Int {
		var init = new nk_init_options();
		init.set_struct_size(nk_init_options.size());
		init.set_api_version(NativeKitConstants.NK_API_VERSION);
		init.set_event_queue_capacity(32);
		if (NativeKit.nk_init(init) != NativeKitConstants.NK_OK)
			return 10;

		var windowOptions = new nk_window_options();
		windowOptions.set_struct_size(nk_window_options.size());
		windowOptions.set_flags(NativeKitConstants.NK_WINDOW_RESIZABLE);
		windowOptions.set_width(256);
		windowOptions.set_height(192);
		windowOptions.set_title("Haxeon: NativeKit UI");
		var createdWindow = NativeKit.nk_window_create(windowOptions);
		if (createdWindow.status != NativeKitConstants.NK_OK) {
			NativeKit.nk_shutdown();
			return 11;
		}
		var window = createdWindow.out_window;
		var surfaceOptions = new nk_surface_options();
		surfaceOptions.set_struct_size(nk_surface_options.size());
		surfaceOptions.set_flags(NativeKitConstants.NK_SURFACE_FORWARD_COMPATIBLE |
			NativeKitConstants.NK_SURFACE_STENCIL);
		surfaceOptions.set_api(NativeKitConstants.NK_GRAPHICS_OPENGL);
		surfaceOptions.set_major_version(3);
		surfaceOptions.set_minor_version(3);
		surfaceOptions.set_width(256);
		surfaceOptions.set_height(192);
		var createdSurface = NativeKit.nk_surface_create(window, surfaceOptions);
		if (createdSurface.status != NativeKitConstants.NK_OK) {
			NativeKit.nk_window_destroy(window);
			NativeKit.nk_shutdown();
			return 12;
		}
		var surface = createdSurface.out_surface;

		var made = NativeKitUI.nkui_display_list_create();
		if (made.status != Nkui_result.NKUI_OK)
			return 1;
		var list = made.out_list;
		var move = new nkui_path_element();
		move.set_verb(Nkui_path_verb.NKUI_PATH_MOVE_TO);
		move.set_values(0, 0.0); move.set_values(1, 0.0);
		var line = new nkui_path_element();
		line.set_verb(Nkui_path_verb.NKUI_PATH_LINE_TO);
		line.set_values(0, 244.0); line.set_values(1, 12.0);
		var down = new nkui_path_element();
		down.set_verb(Nkui_path_verb.NKUI_PATH_LINE_TO);
		down.set_values(0, 244.0); down.set_values(1, 180.0);
		var back = new nkui_path_element();
		back.set_verb(Nkui_path_verb.NKUI_PATH_LINE_TO);
		back.set_values(0, 12.0); back.set_values(1, 180.0);
		var close = new nkui_path_element();
		close.set_verb(Nkui_path_verb.NKUI_PATH_CLOSE);
		var madePath = NativeKitUI.nkui_path_create([move, line, down, back, close]);
		if (madePath.status != Nkui_result.NKUI_OK)
			return 7;
		var path = madePath.out_path;
		var color = new nkui_color();
		color.set_red(0.2); color.set_green(0.6); color.set_blue(0.9); color.set_alpha(1.0);
		var madePaint = NativeKitUI.nkui_paint_create_solid(color);
		var pixels = Bytes.alloc(4);
		pixels.set(0, 20); pixels.set(1, 80); pixels.set(2, 220); pixels.set(3, 255);
		var madeImage = NativeKitUI.nkui_image_create(1, 1, Nkui_image_format.NKUI_IMAGE_RGBA8, pixels);
		var fonts = NativeKitUI.nkui_font_collection_create();
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null || fonts.status != Nkui_result.NKUI_OK ||
			NativeKitUI.nkui_font_collection_add(fonts.out_fonts, fontPath,
				NativeKitUI.Nkui_font_family.NKUI_FONT_FAMILY_DEFAULT) != Nkui_result.NKUI_OK)
			return 9;
		var madeText = NativeKitUI.nkui_text_layout_create(fonts.out_fonts,
			"NativeKit — こんにちは — مرحبا", 220.0, 18.0);
		if (madePaint.status != Nkui_result.NKUI_OK || madeImage.status != Nkui_result.NKUI_OK ||
			madeText.status != Nkui_result.NKUI_OK)
			return 8;
		var commands = new CanvasCommandBuffer(8);
		commands.save();
		commands.transform(1.0, 0.0, 0.0, 1.0, 4.0, 5.0);
		commands.clipRect(0.0, 0.0, 100.0, 80.0);
		commands.globalAlpha(0.5);
		commands.paint(madePaint.out_paint);
		commands.drawPath(path);
		commands.drawText(madeText.out_layout, 16.0, 24.0);
		commands.beginLayer(0.6);
		commands.drawImage(madeImage.out_image, 4.0, 4.0, 12.0, 12.0);
		commands.endLayer();
		commands.restore();
		if (commands.submit(list) != Nkui_result.NKUI_OK)
			return 2;
		var info = NativeKitUI.nkui_display_list_get_info(list);
		if (info.status != Nkui_result.NKUI_OK || info.out_info.get_command_count() != 11 ||
			info.out_info.get_command_bytes() != commands.size())
			return 3;

		var madeRenderer = NativeKitUI.nkui_renderer_create();
		if (madeRenderer.status != Nkui_result.NKUI_OK)
			return 13;
		var renderer = madeRenderer.out_renderer;
		var ready = false;
		var rendered = 0;
		var attempts = 0;
		while (rendered < 3 && attempts < 120) {
			var event = new nk_event();
			event.set_struct_size(nk_event.size());
			var polled = NativeKit.nk_poll_event(event);
			if (polled.status != NativeKitConstants.NK_OK)
				return 14;
			var eventKind = polled.event.get_kind();
			var eventSource = polled.event.get_source();
			NativeKit.nk_event_release(polled.event);
			if (eventKind == NativeKitConstants.NK_EVENT_SURFACE_READY && eventSource == surface)
				ready = true;
			if (ready) {
				var size = NativeKit.nk_surface_get_framebuffer_size(surface);
				if (size.status != NativeKitConstants.NK_OK || size.out_width <= 0 ||
					size.out_height <= 0)
					return 14;
				var scale = NativeKit.nk_window_get_scale(window);
				if (scale.status != NativeKitConstants.NK_OK || scale.out_scale <= 0.0)
					return 15;
				var frame = new nkui_frame_info();
				frame.set_struct_size(nkui_frame_info.size());
				frame.set_logical_width(256.0);
				frame.set_logical_height(192.0);
				frame.set_framebuffer_width(size.out_width);
				frame.set_framebuffer_height(size.out_height);
				frame.set_pixel_scale(scale.out_scale);
				if (NativeKitUI.nkui_renderer_render_frame(renderer, list, surface, frame) !=
					Nkui_result.NKUI_OK || NativeKit.nk_surface_present(surface) !=
					NativeKitConstants.NK_OK)
					return 16;
				rendered++;
			}
			attempts++;
		}
		if (!ready || rendered != 3)
			return 17;
		if (NativeKitUI.nkui_renderer_destroy(renderer) != Nkui_result.NKUI_OK)
			return 18;
		commands.reset();
		if (commands.submit(list) != Nkui_result.NKUI_OK)
			return 4;
		info = NativeKitUI.nkui_display_list_get_info(list);
		if (info.status != Nkui_result.NKUI_OK || info.out_info.get_command_count() != 0)
			return 5;
		var destroyed = NativeKitUI.nkui_display_list_destroy(list);
		NativeKitUI.nkui_resource_destroy(madeText.out_layout);
		NativeKitUI.nkui_resource_destroy(fonts.out_fonts);
		NativeKitUI.nkui_resource_destroy(madeImage.out_image);
		NativeKitUI.nkui_resource_destroy(madePaint.out_paint);
		NativeKitUI.nkui_resource_destroy(path);
		NativeKit.nk_surface_destroy(surface);
		NativeKit.nk_window_destroy(window);
		NativeKit.nk_shutdown();
		return destroyed == Nkui_result.NKUI_OK ? 0 : 6;
	}
}
