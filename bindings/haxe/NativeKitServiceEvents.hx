import NativeKit.NativeKitConstants;
import NativeKitEventBytes;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitServiceEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case NativeKitConstants.NK_EVENT_NONE: None;
		case NativeKitConstants.NK_EVENT_CLIPBOARD_TEXT_COMPLETE: ClipboardText(c.request,c.result,c.data.toString());
		case NativeKitConstants.NK_EVENT_CLIPBOARD_FILES_COMPLETE: ClipboardFiles(c.request,c.result,NativeKitEventBytes.decodeClipboardFiles(c.data,c.dataCount));
		case NativeKitConstants.NK_EVENT_DROP_FILES: DropFiles(c.source,NativeKitEventBytes.decodeDropItems(c.data,c.dataCount));
		case NativeKitConstants.NK_EVENT_DROP_TEXT: DropText(c.source,NativeKitEventBytes.decodeDropItems(c.data,c.dataCount).join(""));
		case NativeKitConstants.NK_EVENT_DIALOG_COMPLETE:
			if(c.flags==NativeKitConstants.NK_BINDING_DIALOG_MESSAGE) { NativeKitEventBytes.requireSize(c.data,4); DialogMessage(c.request,c.result,NativeKitEventBytes.readU32(c.data,0)); }
			else DialogPaths(c.request,c.result,NativeKitEventBytes.readU32(c.data,0)!=0,NativeKitEventBytes.decodeDialogPaths(c.data));
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
}
