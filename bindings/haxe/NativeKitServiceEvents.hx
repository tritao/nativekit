import NativeKit.EventKind;
import NativeKitEventBytes;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitServiceEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case EventKind.None: None;
		case EventKind.ClipboardTextComplete: ClipboardText(c.request,c.result,c.data.toString());
		case EventKind.ClipboardFilesComplete: ClipboardFiles(c.request,c.result,NativeKitEventBytes.decodeClipboardFiles(c.data,c.dataCount));
		case EventKind.DropFiles: DropFiles(c.source,NativeKitEventBytes.decodeDropItems(c.data,c.dataCount));
		case EventKind.DropText: DropText(c.source,NativeKitEventBytes.decodeDropItems(c.data,c.dataCount).join(""));
		case EventKind.DialogPathsComplete:
			DialogPaths(c.request,c.result,NativeKitEventBytes.readU32(c.data,0)!=0,NativeKitEventBytes.decodeDialogPaths(c.data));
		case EventKind.DialogMessageComplete:
			NativeKitEventBytes.requireSize(c.data,4); DialogMessage(c.request,c.result,NativeKitEventBytes.readU32(c.data,0));
		case EventKind.WebviewNavigated: WebViewNavigated(c.source,c.data.toString());
		case EventKind.WebviewMessage: WebViewMessage(c.source,c.data.toString());
		case EventKind.WebviewTitleChanged: WebViewTitleChanged(c.source,c.data.toString());
		case EventKind.WebviewEvalComplete: WebViewEvaluation(c.source,c.request,c.result,c.data.toString());
		case EventKind.WebviewNavigationFailed: WebViewNavigationFailed(c.source,c.flags,c.data.toString());
		case EventKind.WebviewNavigationRequest: WebViewNavigationRequest(c.source,c.request,c.data.toString());
		case EventKind.NotificationActivated: NotificationActivated(c.request,c.data.toString());
		case EventKind.NotificationFailed: NotificationFailed(c.request,c.data.toString());
		default:null;
	}
}
