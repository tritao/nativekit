import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;
import NativeKitEvent;
import NativeKitEventValue;

/** The managed owner of NativeKit's native event queue. */
class NativeKitEvents {
	public final requests:NativeKitRequests;
	final listeners:Array<NativeKitEventValue->Void> = [];
	var disposed:Bool = false;

	@:allow(NativeKitRuntime)
	@:allow(ShowcaseDesktop)
	@:allow(ShowcaseWeb)
	private function new() {
		requests = new NativeKitRequests();
	}

	/** Polls and releases one native event, then routes its managed snapshot. */
	public function poll():Bool {
		ensureLive();
		var polled = NativeKit.nk_poll_event_checked(new Event());
		var event = new NativeKitEvent(polled);
		var value = event.take();
		var hasEvent = switch value {
			case None: false;
			case _: true;
		};
		if (hasEvent)
			dispatch(value);
		return hasEvent;
	}

	/** Pumps native events and waits until input or the timeout expires. */
	public function wait(timeoutSeconds:Float):Void {
		ensureLive();
		NativeKit.nk_wait_events_timeout_checked(timeoutSeconds);
	}

	/** Adds an observer which receives decoded, fully managed events. */
	public function listen(listener:NativeKitEventValue->Void):NativeKitEventSubscription {
		ensureLive();
		if (listener == null)
			throw "NativeKit event listeners cannot be null";
		listeners.push(listener);
		return new NativeKitEventSubscription(this, listener);
	}

	/** Reports whether this runtime's event pump has been shut down. */
	public function isDisposed():Bool
		return disposed;

	/** Removes one listener when its subscription is disposed. */
	@:allow(NativeKitEventSubscription)
	private function removeListener(listener:NativeKitEventValue->Void):Bool
		return listeners.remove(listener);

	/** Stops the pump after its owning runtime shuts down. */
	@:allow(NativeKitRuntime)
	@:allow(ShowcaseDesktop)
	@:allow(ShowcaseWeb)
	private function runtimeShutdown():Void {
		disposed = true;
		listeners.resize(0);
	}

	/** Routes a decoded value through the same request and listener path as poll(). */
	public function dispatch(value:NativeKitEventValue):Void {
		ensureLive();
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

	function ensureLive():Void {
		if (disposed)
			throw "NativeKit event pump has been disposed with its runtime";
	}
}

/** One listener registration owned by a NativeKit event pump. */
class NativeKitEventSubscription {
	final events:NativeKitEvents;
	final listener:NativeKitEventValue->Void;
	var disposed:Bool = false;

	@:allow(NativeKitEvents)
	private function new(events:NativeKitEvents, listener:NativeKitEventValue->Void) {
		this.events = events;
		this.listener = listener;
	}

	public function dispose():Void {
		if (disposed)
			return;
		disposed = true;
		events.removeListener(listener);
	}

	public function isDisposed():Bool
		return disposed || events.isDisposed();
}
