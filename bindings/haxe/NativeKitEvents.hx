import NativeKit;
import NativeKit.Event;
import NativeKit.Result;
import NativeKitEvent;
import NativeKitEventValue;

/** The managed owner of NativeKit's native event queue. */
class NativeKitEvents {
	public final requests:NativeKitRequests;
	final listeners:Array<NativeKitEventValue->Void> = [];

	public function new(?requests:NativeKitRequests) {
		this.requests = requests == null ? new NativeKitRequests() : requests;
	}

	/** Polls and releases one native event, then routes its managed snapshot. */
	public function poll():Bool {
		var nativeEvent = new Event();
		nativeEvent.set_struct_size(Event.size());
		var polled = NativeKit.nk_poll_event(nativeEvent);
		if (polled.status != Result.Ok)
			throw 'NativeKit event poll failed: ${polled.status}';
		var event = new NativeKitEvent(polled.event);
		var value = event.take();
		var hasEvent = switch value {
			case None: false;
			case _: true;
		};
		if (hasEvent)
			dispatch(value);
		return hasEvent;
	}

	/** Adds an observer which receives decoded, fully managed events. */
	public function addListener(listener:NativeKitEventValue->Void):Void {
		if (listener == null)
			throw "NativeKit event listeners cannot be null";
		listeners.push(listener);
	}

	/** Removes one observer. Returns false when it was not attached. */
	public function removeListener(listener:NativeKitEventValue->Void):Bool
		return listeners.remove(listener);

	/** Routes a decoded value through the same request and listener path as poll(). */
	public function dispatch(value:NativeKitEventValue):Void {
		if (value == null)
			return;
		switch value {
			case None:
				return;
			case _:
		}

		var snapshot = listeners.copy();
		var failure:Dynamic = null;
		try {
			requests.handle(value);
		} catch (error:Dynamic) {
			failure = error;
		}
		for (listener in snapshot) {
			try {
				listener(value);
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		if (failure != null)
			throw failure;
	}
}
