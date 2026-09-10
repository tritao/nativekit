import NativeKitUI;
import NativeKitUI.Nkui_image_format;
import NativeKitUI.Nkui_path_verb;
import NativeKitUI.Nkui_result;
import haxe.io.Bytes;

class Transaction {
	static function main():Int {
		var made = NativeKitUI.nkui_display_list_create();
		if (made.status != 0)
			return 1;
		var list = made.out_list;
		var move = new nkui_path_element();
		move.set_verb(Nkui_path_verb.NKUI_PATH_MOVE_TO);
		move.set_values(0, 0.0); move.set_values(1, 0.0);
		var line = new nkui_path_element();
		line.set_verb(Nkui_path_verb.NKUI_PATH_LINE_TO);
		line.set_values(0, 32.0); line.set_values(1, 32.0);
		var madePath = NativeKitUI.nkui_path_create([move, line]);
		if (madePath.status != Nkui_result.NKUI_OK)
			return 7;
		var path = madePath.out_path;
		var color = new nkui_color();
		color.set_red(0.2); color.set_green(0.6); color.set_blue(0.9); color.set_alpha(1.0);
		var madePaint = NativeKitUI.nkui_paint_create_solid(color);
		var pixels = Bytes.alloc(4);
		pixels.set(0, 20); pixels.set(1, 80); pixels.set(2, 220); pixels.set(3, 255);
		var madeImage = NativeKitUI.nkui_image_create(1, 1, Nkui_image_format.NKUI_IMAGE_RGBA8, pixels);
		if (madePaint.status != Nkui_result.NKUI_OK || madeImage.status != Nkui_result.NKUI_OK)
			return 8;
		var commands = new CanvasCommandBuffer(8);
		commands.save();
		commands.transform(1.0, 0.0, 0.0, 1.0, 4.0, 5.0);
		commands.clipRect(0.0, 0.0, 100.0, 80.0);
		commands.globalAlpha(0.5);
		commands.paint(madePaint.out_paint);
		commands.beginLayer(0.6);
		commands.drawPath(path);
		commands.drawImage(madeImage.out_image, 4.0, 4.0, 12.0, 12.0);
		commands.endLayer();
		commands.restore();
		if (commands.submit(list) != 0)
			return 2;
		var info = NativeKitUI.nkui_display_list_get_info(list);
		if (info.status != 0 || info.out_info.get_command_count() != 10 ||
			info.out_info.get_command_bytes() != commands.size())
			return 3;
		commands.reset();
		if (commands.submit(list) != 0)
			return 4;
		info = NativeKitUI.nkui_display_list_get_info(list);
		if (info.status != 0 || info.out_info.get_command_count() != 0)
			return 5;
		var destroyed = NativeKitUI.nkui_display_list_destroy(list);
		NativeKitUI.nkui_resource_destroy(madeImage.out_image);
		NativeKitUI.nkui_resource_destroy(madePaint.out_paint);
		NativeKitUI.nkui_resource_destroy(path);
		return destroyed == Nkui_result.NKUI_OK ? 0 : 6;
	}
}
