import NativeKitUI;

class Transaction {
	static function main():Int {
		var made = NativeKitUI.nkui_display_list_create();
		if (made.status != 0)
			return 1;
		var list = made.out_list;
		var path = new nkui_resource();
		path.set_id((1 << 28) | (1 << 16) | 1);
		var commands = new CanvasCommandBuffer(8);
		commands.save();
		commands.transform(1.0, 0.0, 0.0, 1.0, 4.0, 5.0);
		commands.clipRect(0.0, 0.0, 100.0, 80.0);
		commands.globalAlpha(0.5);
		commands.beginLayer(0.6);
		commands.drawPath(path);
		commands.endLayer();
		commands.restore();
		if (commands.submit(list) != 0)
			return 2;
		var info = NativeKitUI.nkui_display_list_get_info(list);
		if (info.status != 0 || info.out_info.get_command_count() != 8 ||
			info.out_info.get_command_bytes() != commands.size())
			return 3;
		commands.reset();
		if (commands.submit(list) != 0)
			return 4;
		info = NativeKitUI.nkui_display_list_get_info(list);
		if (info.status != 0 || info.out_info.get_command_count() != 0)
			return 5;
		return NativeKitUI.nkui_display_list_destroy(list) == 0 ? 0 : 6;
	}
}
