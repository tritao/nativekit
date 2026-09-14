import NativeKit;
import NativeKit.InitOptions;
import NativeKit.NativeKitConstants;
import NativeKit.MessageKind;
import NativeKit.MessageButtons;
import NativeKit.WindowKind;
import NativeKit.WindowFlags;
import NativeKit.SurfaceFlags;
import NativeKit.GraphicsApi;
import NativeKit.TextInputFlags;
import NativeKitSurface;
import NativeKitWindow;
import NativeKit.DialogFlags;
import NativeKit.WebviewFlags;
import NativeKit.ResourceFlags;
import NativeKit.DialogFilter;
import NativeKit.FileDialogOptions;
import NativeKit.MessageDialogOptions;
import NativeKit.NotificationOptions;
import NativeKit.NotificationFlags;
import NativeKit.Resource;
import NativeKit.ShareOptions;
import NativeKit.WebviewOptions;
import NativeKit.WindowOptions;
import NativeKit.WindowHandle;
import NativeKit.SurfaceHandle;

/** Creates correctly sized NativeKit option structures with useful defaults. */
class NativeKitOptions {
	public static function init(?eventQueueCapacity:Int):InitOptions {
		var value = new InitOptions();
		value.set_api_version(NativeKitConstants.NK_API_VERSION);
		value.set_event_queue_capacity(eventQueueCapacity == null ? 0 : eventQueueCapacity);
		return value;
	}

	/** Builds a file dialog with a generated, retained filter array. */
	public static function filteredFileDialog(filters:Array<DialogFilter>, ?title:String,
			?initialPath:String, ?suggestedName:String, ?flags:DialogFlags):FileDialogOptions {
		var options = rawFileDialog(title, initialPath, suggestedName, flags);
		options.set_filters(filters);
		return options;
	}

	/** Creates one dialog filter with managed UTF-8 storage. */
	public static function dialogFilter(patterns:String, ?name:String):DialogFilter {
		var value = new DialogFilter();
		value.set_patterns(patterns);
		value.set_name(name);
		return value;
	}

	public static function window(width:Int, height:Int, ?title:String, ?flags:WindowFlags,
			?owner:NativeKitWindow, ?kind:WindowKind):WindowOptions {
		var value = new WindowOptions();
		value.set_width(width);
		value.set_height(height);
		value.set_title(title);
		value.set_flags(flags == null ? WindowFlags.Resizable : flags);
		value.set_owner(owner == null ? WindowHandle.invalid() : owner.nativeHandle());
		value.set_kind(kind == null ? WindowKind.Normal : kind);
		return value;
	}

	public static function surface(api:GraphicsApi, x:Int, y:Int, width:Int, height:Int,
			?flags:SurfaceFlags, ?majorVersion:Int, ?minorVersion:Int, ?shareSurface:NativeKitSurface):SurfaceOptions {
		var value = new SurfaceOptions();
		value.set_flags(flags == null ? 0 : flags);
		value.set_api(api);
		value.set_major_version(majorVersion == null ? 0 : majorVersion);
		value.set_minor_version(minorVersion == null ? 0 : minorVersion);
		value.set_x(x);
		value.set_y(y);
		value.set_width(width);
		value.set_height(height);
		value.set_share_surface(shareSurface == null ? SurfaceHandle.invalid() : shareSurface.nativeHandle());
		return value;
	}

	public static function webview(x:Int, y:Int, width:Int, height:Int,
			?initialUrl:String, ?flags:WebviewFlags):WebviewOptions {
		var value = new WebviewOptions();
		value.set_x(x);
		value.set_y(y);
		value.set_width(width);
		value.set_height(height);
		value.set_initial_url(initialUrl);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function fileDialog(?title:String, ?initialPath:String,
			?suggestedName:String, ?flags:DialogFlags):FileDialogOptions
		return filteredFileDialog([], title, initialPath, suggestedName, flags);

	static function rawFileDialog(?title:String, ?initialPath:String,
		?suggestedName:String, ?flags:DialogFlags):FileDialogOptions {
		var value = new FileDialogOptions();
		value.set_title(title);
		value.set_initial_path(initialPath);
		value.set_suggested_name(suggestedName);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function messageDialog(message:String, ?title:String, ?kind:MessageKind,
			?buttons:MessageButtons):MessageDialogOptions {
		var value = new MessageDialogOptions();
		value.set_message(message);
		value.set_title(title);
		value.set_kind(kind == null ? MessageKind.Info : kind);
		value.set_buttons(buttons == null ? MessageButtons.Ok : buttons);
		return value;
	}

	public static function resource(uri:String, ?mimeType:String, ?displayName:String,
			?flags:ResourceFlags):Resource {
		var value = new Resource();
		value.set_uri(uri);
		value.set_mime_type(mimeType);
		value.set_display_name(displayName);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}

	public static function textShare(text:String, ?title:String):ShareOptions {
		var value = new ShareOptions();
		value.set_text(text);
		value.set_title(title);
		value.set_flags(0);
		return value;
	}

	/** Builds share options with a generated, retained resource array. */
	public static function resourceShare(resources:Array<Resource>, ?text:String,
		?title:String):NativeKitShareOptions {
		var options = new ShareOptions();
		options.set_text(text);
		options.set_title(title);
		options.set_flags(0);
		options.set_resources(resources);
		return new NativeKitShareOptions(options);
	}

	public static function notification(title:String, ?body:String, ?icon:String,
			?timeoutMs:Int, ?flags:NotificationFlags):NotificationOptions {
		var value = new NotificationOptions();
		value.set_title(title);
		value.set_body(body);
		value.set_icon(icon);
		value.set_timeout_ms(timeoutMs == null ? 0 : timeoutMs);
		value.set_flags(flags == null ? 0 : flags);
		return value;
	}
}

class NativeKitShareOptions {
	public final options:ShareOptions;

	public function new(options:ShareOptions) {
		this.options = options;
	}

	public function submit():Void {
		NativeKit.nk_share_checked(options);
	}
}
