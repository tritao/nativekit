import NativeKit.NativeKitConstants;
import NativeKit.NkEventKind;
import NativeKitEventBytes;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitServiceEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case NkEventKind.None: None;
		case NkEventKind.ClipboardTextComplete: ClipboardText(c.request,c.result,c.data.toString());
		case NkEventKind.ClipboardFilesComplete: ClipboardFiles(c.request,c.result,NativeKitEventBytes.decodeClipboardFiles(c.data,c.dataCount));
		case NkEventKind.DropFiles: DropFiles(c.source,NativeKitEventBytes.decodeDropItems(c.data,c.dataCount));
		case NkEventKind.DropText: DropText(c.source,NativeKitEventBytes.decodeDropItems(c.data,c.dataCount).join(""));
		case NkEventKind.DialogPathsComplete:
			DialogPaths(c.request,c.result,NativeKitEventBytes.readU32(c.data,0)!=0,NativeKitEventBytes.decodeDialogPaths(c.data));
		case NkEventKind.DialogMessageComplete:
			NativeKitEventBytes.requireSize(c.data,4); DialogMessage(c.request,c.result,NativeKitEventBytes.readU32(c.data,0));
		case NkEventKind.WebviewNavigated: WebViewNavigated(c.source,c.data.toString());
		case NkEventKind.WebviewMessage: WebViewMessage(c.source,c.data.toString());
		case NkEventKind.WebviewTitleChanged: WebViewTitleChanged(c.source,c.data.toString());
		case NkEventKind.WebviewEvalComplete: WebViewEvaluation(c.source,c.request,c.result,c.data.toString());
		case NkEventKind.WebviewNavigationFailed: WebViewNavigationFailed(c.source,c.flags,c.data.toString());
		case NkEventKind.WebviewNavigationRequest: WebViewNavigationRequest(c.source,c.request,c.data.toString());
		case NkEventKind.NotificationActivated: NotificationActivated(c.request,c.data.toString());
		case NkEventKind.NotificationFailed: NotificationFailed(c.request,c.data.toString());
		default:null;
	}
}
