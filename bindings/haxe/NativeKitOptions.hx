import NativeKit;
import NativeKit.MessageKind;
import NativeKit.MessageButtons;
import NativeKit.Result;
import NativeKit.WindowKind;
import NativeKit.WindowFlags;
import NativeKit.DialogFlags;
import NativeKit.WebviewFlags;
import NativeKit.ResourceFlags;
import NativeKit.DialogFilter;
import NativeKit.FileDialogOptions;
import NativeKit.MessageDialogOptions;
import NativeKit.NotificationOptions;
import NativeKit.Resource;
import NativeKit.ShareOptions;
import NativeKit.WebviewOptions;
import NativeKit.WindowOptions;

/** Creates correctly sized NativeKit option structures with useful defaults. */
class NativeKitOptions {
	/** Attaches a managed contiguous filter array and keeps it alive with the options. */
	public static function filteredFileDialog(filters:Array<DialogFilter>, ?title:String,
			?initialPath:String, ?suggestedName:String, ?flags:DialogFlags):NativeKitFileDialogOptions {
		var options = rawFileDialog(title, initialPath, suggestedName, flags);
		var storage = DialogFilter.array(filters);
		if (filters.length > 0) options.set_filters(storage);
		options.set_filter_count(filters.length);
		return new NativeKitFileDialogOptions(options, storage);
	}

	/** Creates one dialog filter with managed UTF-8 storage. */
	public static function dialogFilter(patterns:String, ?name:String):DialogFilter {
		var value = new DialogFilter();
		value.set_patterns(patterns);
		value.set_name(name);
		return value;
	}

	public static function window(width:Int, height:Int, ?title:String, ?flags:WindowFlags,
			?owner:Int, ?kind:Int):WindowOptions {
		var value = new WindowOptions();
		value.set_struct_size(WindowOptions.size());
		value.set_width(width);
		value.set_height(height);
		value.set_title(title);
		value.set_flags(flags == null ? WindowFlags.Resizable : flags);
		value.set_owner(owner == null ? 0 : owner);
		value.set_kind(kind == null ? WindowKind.Normal : kind);
		return value;
	}

	public static function webview(x:Int, y:Int, width:Int, height:Int,
			?initialUrl:String, ?flags:WebviewFlags):WebviewOptions {
		var value = new WebviewOptions();
		value.set_struct_size(WebviewOptions.size());
		value.set_x(x);
		value.set_y(y);
		value.set_width(width);
		value.set_height(height);
		value.set_initial_url(initialUrl);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function fileDialog(?title:String, ?initialPath:String,
			?suggestedName:String, ?flags:DialogFlags):NativeKitFileDialogOptions
		return filteredFileDialog([], title, initialPath, suggestedName, flags);

	static function rawFileDialog(?title:String, ?initialPath:String,
		?suggestedName:String, ?flags:DialogFlags):FileDialogOptions {
		var value = new FileDialogOptions();
		value.set_struct_size(FileDialogOptions.size());
		value.set_title(title);
		value.set_initial_path(initialPath);
		value.set_suggested_name(suggestedName);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function messageDialog(message:String, ?title:String, ?kind:Int,
			?buttons:MessageButtons):MessageDialogOptions {
		var value = new MessageDialogOptions();
		value.set_struct_size(MessageDialogOptions.size());
		value.set_message(message);
		value.set_title(title);
		value.set_kind(kind == null ? MessageKind.Info : kind);
		value.set_buttons(buttons == null ? MessageButtons.Ok : buttons);
		return value;
	}

	public static function resource(uri:String, ?mimeType:String, ?displayName:String,
			?flags:ResourceFlags):Resource {
		var value = new Resource();
		value.set_struct_size(Resource.size());
		value.set_uri(uri);
		value.set_mime_type(mimeType);
		value.set_display_name(displayName);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function textShare(text:String, ?title:String, ?flags:Int):ShareOptions {
		var value = new ShareOptions();
		value.set_struct_size(ShareOptions.size());
		value.set_text(text);
		value.set_title(title);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	/** Attaches managed contiguous resources and keeps them alive with the share options. */
	public static function resourceShare(resources:Array<Resource>, ?text:String,
			?title:String, ?flags:Int):NativeKitShareOptions {
		var options = new ShareOptions();
		options.set_struct_size(ShareOptions.size());
		options.set_text(text);
		options.set_title(title);
		options.set_flags(flags == null ? 0 : flags);
		var storage = Resource.array(resources);
		if (resources.length > 0) options.set_resources(storage);
		options.set_resource_count(resources.length);
		return new NativeKitShareOptions(options, storage);
	}

	public static function notification(title:String, ?body:String, ?icon:String,
			?timeoutMs:Int, ?flags:Int):NotificationOptions {
		var value = new NotificationOptions();
		value.set_struct_size(NotificationOptions.size());
		value.set_title(title);
		value.set_body(body);
		value.set_icon(icon);
		value.set_timeout_ms(timeoutMs == null ? 0 : timeoutMs);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}
}

class NativeKitFileDialogOptions {
	public final options:FileDialogOptions;
	final filters:DialogFilter;

	public function new(options:FileDialogOptions, filters:DialogFilter) {
		this.options = options;
		this.filters = filters;
	}
}

class NativeKitShareOptions {
	public final options:ShareOptions;
	final resources:Resource;

	public function new(options:ShareOptions, resources:Resource) {
		this.options = options;
		this.resources = resources;
	}

	public function submit():Void {
		var result = NativeKit.nk_share(options);
		if (result != Result.Ok)
			throw 'NativeKit share failed: $result';
	}
}
