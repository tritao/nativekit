import NativeKit.NativeKitConstants;
import NativeKitEvent;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitServiceEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case NativeKitConstants.NK_EVENT_NONE: None;
		case NativeKitConstants.NK_EVENT_CLIPBOARD_TEXT_COMPLETE: ClipboardText(c.request,c.result,c.data.toString());
		case NativeKitConstants.NK_EVENT_CLIPBOARD_FILES_COMPLETE: ClipboardFiles(c.request,c.result,NativeKitEvent.decodeClipboardFiles(c.data,c.dataCount));
		case NativeKitConstants.NK_EVENT_DROP_FILES: DropFiles(c.source,NativeKitEvent.decodeDropItems(c.data,c.dataCount));
		case NativeKitConstants.NK_EVENT_DROP_TEXT: DropText(c.source,NativeKitEvent.decodeDropItems(c.data,c.dataCount).join(""));
		case NativeKitConstants.NK_EVENT_DIALOG_COMPLETE:
			if(c.flags==NativeKitConstants.NK_BINDING_DIALOG_MESSAGE) { if(c.data.length!=4) throw "Invalid message dialog payload"; DialogMessage(c.request,c.result,u32(c.data,0)); }
			else DialogPaths(c.request,c.result,u32(c.data,0)!=0,NativeKitEvent.decodeDialogPaths(c.data));
		case NativeKitConstants.NK_EVENT_WEBVIEW_NAVIGATED: WebViewNavigated(c.source,c.data.toString());
		case NativeKitConstants.NK_EVENT_WEBVIEW_MESSAGE: WebViewMessage(c.source,c.data.toString());
		case NativeKitConstants.NK_EVENT_WEBVIEW_TITLE_CHANGED: WebViewTitleChanged(c.source,c.data.toString());
		case NativeKitConstants.NK_EVENT_WEBVIEW_EVAL_COMPLETE: WebViewEvaluation(c.source,c.request,c.result,c.data.toString());
		case NativeKitConstants.NK_EVENT_WEBVIEW_NAVIGATION_FAILED: WebViewNavigationFailed(c.source,c.flags,c.data.toString());
		case NativeKitConstants.NK_EVENT_WEBVIEW_NAVIGATION_REQUEST: WebViewNavigationRequest(c.source,c.request,c.data.toString());
		case NativeKitConstants.NK_EVENT_NOTIFICATION_ACTIVATED: NotificationActivated(c.request,c.data.toString());
		case NativeKitConstants.NK_EVENT_NOTIFICATION_FAILED: NotificationFailed(c.request,c.data.toString());
		default:null;
	}
	static function u32(b:haxe.io.Bytes,o:Int):Int return b.get(o)|b.get(o+1)<<8|b.get(o+2)<<16|b.get(o+3)<<24;
}
