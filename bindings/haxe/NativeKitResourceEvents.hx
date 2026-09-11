import NativeKit.EventKind;
import NativeKit.ResourceDrop;
import NativeKitEventBytes;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitResourceEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case EventKind.DialogResourcesComplete:
			var d=NativeKitEventBytes.decodeResourceList(c.data,0); Resources(c.kind,c.request,c.result,d.accepted,d.items);
		case EventKind.ClipboardResourcesComplete | EventKind.ResourceOpened:
			var d=NativeKitEventBytes.decodeResourceList(c.data,0); Resources(c.kind,c.request,c.result,d.accepted,d.items);
		case EventKind.ShareReceived:
			var o=NativeKitEventBytes.readU32(c.data,0),d=NativeKitEventBytes.decodeResourceList(c.data,o);
			ShareReceived(NativeKitEventBytes.readOptionalString(c.data,NativeKitEventBytes.readU32(c.data,4),16),NativeKitEventBytes.readOptionalString(c.data,NativeKitEventBytes.readU32(c.data,8),16),d.items);
		case EventKind.ResourceDrop:
			if(c.data.length<32) throw "NativeKit resource drop payload is truncated";
			var h:ResourceDrop=c.data,d=NativeKitEventBytes.decodeResourceList(c.data,h.get_resources_offset());
			ResourceDrop(c.source,h.get_x(),h.get_y(),NativeKitEventBytes.readOptionalString(c.data,h.get_text_offset(),32),d.items);
		default:null;
	}
}
