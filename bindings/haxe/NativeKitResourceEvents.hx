import NativeKit.NativeKitConstants;
import NativeKitEvent;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitResourceEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> return switch c.kind {
		case NativeKitConstants.NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE | NativeKitConstants.NK_EVENT_RESOURCE_OPENED:
			var d=NativeKitEvent.decodeResourceList(c.data,0); Resources(c.kind,c.request,c.result,d.accepted,d.items);
		case NativeKitConstants.NK_EVENT_SHARE_RECEIVED:
			var o=NativeKitEvent.readEventU32(c.data,0),d=NativeKitEvent.decodeResourceList(c.data,o);
			ShareReceived(NativeKitEvent.readEventString(c.data,NativeKitEvent.readEventU32(c.data,4),16),NativeKitEvent.readEventString(c.data,NativeKitEvent.readEventU32(c.data,8),16),d.items);
		case NativeKitConstants.NK_EVENT_RESOURCE_DROP:
			if(c.data.length<32) throw "NativeKit resource drop payload is truncated";
			var h:nk_resource_drop=c.data,d=NativeKitEvent.decodeResourceList(c.data,h.get_resources_offset());
			ResourceDrop(c.source,h.get_x(),h.get_y(),NativeKitEvent.readEventString(c.data,h.get_text_offset(),32),d.items);
		default:null;
	}
}
