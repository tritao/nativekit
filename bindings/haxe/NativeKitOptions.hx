import NativeKit;
import NativeKit.NativeKitConstants;

/** Creates correctly sized NativeKit option structures with useful defaults. */
class NativeKitOptions {
	/** Attaches a managed contiguous filter array and keeps it alive with the options. */
	public static function filteredFileDialog(filters:Array<nk_dialog_filter>, ?title:String,
			?initialPath:String, ?suggestedName:String, ?flags:Int):NativeKitFileDialogOptions {
		var options = fileDialog(title, initialPath, suggestedName, flags);
		var storage = nk_dialog_filter.array(filters);
		if (filters.length > 0) options.set_filters(storage);
		options.set_filter_count(filters.length);
		return new NativeKitFileDialogOptions(options, storage);
	}

	/** Creates one dialog filter with managed UTF-8 storage. */
	public static function dialogFilter(patterns:String, ?name:String):nk_dialog_filter {
		var value = new nk_dialog_filter();
		value.set_patterns(patterns);
		value.set_name(name);
		return value;
	}

	public static function window(width:Int, height:Int, ?title:String, ?flags:Int,
			?owner:Int, ?kind:Int):nk_window_options {
		var value = new nk_window_options();
		value.set_struct_size(nk_window_options.size());
		value.set_width(width);
		value.set_height(height);
		value.set_title(title);
		value.set_flags(flags == null ? NativeKitConstants.NK_WINDOW_RESIZABLE : flags);
		value.set_owner(owner == null ? 0 : owner);
		value.set_kind(kind == null ? NativeKitConstants.NK_WINDOW_NORMAL : kind);
		return value;
	}

	public static function webview(x:Int, y:Int, width:Int, height:Int,
			?initialUrl:String, ?flags:Int):nk_webview_options {
		var value = new nk_webview_options();
		value.set_struct_size(nk_webview_options.size());
		value.set_x(x);
		value.set_y(y);
		value.set_width(width);
		value.set_height(height);
		value.set_initial_url(initialUrl);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function fileDialog(?title:String, ?initialPath:String,
			?suggestedName:String, ?flags:Int):nk_file_dialog_options {
		var value = new nk_file_dialog_options();
		value.set_struct_size(nk_file_dialog_options.size());
		value.set_title(title);
		value.set_initial_path(initialPath);
		value.set_suggested_name(suggestedName);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function messageDialog(message:String, ?title:String, ?kind:Int,
			?buttons:Int):nk_message_dialog_options {
		var value = new nk_message_dialog_options();
		value.set_struct_size(nk_message_dialog_options.size());
		value.set_message(message);
		value.set_title(title);
		value.set_kind(kind == null ? NativeKitConstants.NK_MESSAGE_INFO : kind);
		value.set_buttons(buttons == null ? NativeKitConstants.NK_MESSAGE_BUTTON_OK : buttons);
		return value;
	}

	public static function resource(uri:String, ?mimeType:String, ?displayName:String,
			?flags:Int):nk_resource {
		var value = new nk_resource();
		value.set_struct_size(nk_resource.size());
		value.set_uri(uri);
		value.set_mime_type(mimeType);
		value.set_display_name(displayName);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function textShare(text:String, ?title:String, ?flags:Int):nk_share_options {
		var value = new nk_share_options();
		value.set_struct_size(nk_share_options.size());
		value.set_text(text);
		value.set_title(title);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	/** Attaches managed contiguous resources and keeps them alive with the share options. */
	public static function resourceShare(resources:Array<nk_resource>, ?text:String,
			?title:String, ?flags:Int):NativeKitShareOptions {
		var options = new nk_share_options();
		options.set_struct_size(nk_share_options.size());
		options.set_text(text);
		options.set_title(title);
		options.set_flags(flags == null ? 0 : flags);
		var storage = nk_resource.array(resources);
		if (resources.length > 0) options.set_resources(storage);
		options.set_resource_count(resources.length);
		return new NativeKitShareOptions(options, storage);
	}

	public static function notification(title:String, ?body:String, ?icon:String,
			?timeoutMs:Int, ?flags:Int):nk_notification_options {
		var value = new nk_notification_options();
		value.set_struct_size(nk_notification_options.size());
		value.set_title(title);
		value.set_body(body);
		value.set_icon(icon);
		value.set_timeout_ms(timeoutMs == null ? 0 : timeoutMs);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}
}

class NativeKitFileDialogOptions {
	public final options:nk_file_dialog_options;
	final filters:nk_dialog_filter;

	public function new(options:nk_file_dialog_options, filters:nk_dialog_filter) {
		this.options = options;
		this.filters = filters;
	}
}

class NativeKitShareOptions {
	public final options:nk_share_options;
	final resources:nk_resource;

	public function new(options:nk_share_options, resources:nk_resource) {
		this.options = options;
		this.resources = resources;
	}

	public function submit():Void {
		var result = NativeKit.nk_share(options);
		if (result != NativeKitConstants.NK_OK)
			throw 'NativeKit share failed: $result';
	}
}
