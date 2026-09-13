import NativeKit;
import NativeKit.SurfaceHandle;
import NativeKit.SurfaceFrameCallbackCallback;
import NativeKitResult;

/** Owns one graphics surface attached to a NativeKit window. */
class NativeKitSurface {
	final value:SurfaceHandle;
	final ownsHandle:Bool;
	var disposed:Bool = false;
	var frameSubscription:Null<NativeKitSurfaceFrameSubscription>;

	@:allow(NativeKitWindow)
	private function new(value:SurfaceHandle, ownsHandle:Bool = true) {
		this.value = value;
		this.ownsHandle = ownsHandle;
	}

	/** Creates a non-owning view of a surface managed by a raw NativeKit host. */
	public static function borrowNativeHandle(value:SurfaceHandle):NativeKitSurface {
		if (!value.isValid())
			throw "Cannot borrow a null NativeKit surface handle";
		return new NativeKitSurface(value, false);
	}

	public function nativeHandle():SurfaceHandle {
		ensureLive();
		return value;
	}

	public function show(visible:Bool = true):Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_surface_show(value, visible), "surface.show");
	}

	public function makeCurrent():Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_surface_make_current(value), "surface.makeCurrent");
	}

	public function present():Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_surface_present(value), "surface.present");
	}

	/** Replaces the current frame handler and roots it until dispose/detach succeeds. */
	public function onFrame(handler:Int->Int->Void):NativeKitSurfaceFrameSubscription {
		ensureLive();
		if (frameSubscription != null)
			frameSubscription.dispose();
		var handlerBridge = new NativeKitSurfaceFrameHandler(handler);
		var callback = new SurfaceFrameCallbackCallback(handlerBridge.invoke);
		var status = NativeKit.nk_surface_set_frame_callback(value, callback, null);
		if (status != NativeKit.Result.Ok) {
			callback.close();
			NativeKitResult.check(status, "surface.onFrame");
		}
		var subscription = new NativeKitSurfaceFrameSubscription(this, callback);
		frameSubscription = subscription;
		return subscription;
	}

	public function dispose():Void {
		if (disposed)
			return;
		if (frameSubscription != null)
			frameSubscription.dispose();
		if (ownsHandle)
			NativeKitResult.check(NativeKit.nk_surface_destroy(value), "surface.dispose");
		disposed = true;
	}

	/** Releases a borrowed wrapper without destroying its host-owned surface. */
	public function releaseBorrowed():Void {
		if (ownsHandle)
			throw "Cannot release a borrowed view of an owned NativeKit surface";
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(NativeKitSurfaceFrameSubscription)
	function detach(subscription:NativeKitSurfaceFrameSubscription):Void {
		if (frameSubscription != subscription) {
			subscription.releasedAfterDetach();
			return;
		}
		NativeKitResult.check(NativeKit.nk_surface_set_frame_callback(value, null, null), "surface.detachFrameCallback");
		frameSubscription = null;
		subscription.releasedAfterDetach();
	}

	@:allow(NativeKitWindow)
	function runtimeShutdown():Void {
		if (frameSubscription != null) {
			frameSubscription.releasedAfterRuntimeShutdown();
			frameSubscription = null;
		}
		disposed = true;
	}

	function ensureLive():Void {
		if (disposed)
			throw "NativeKit surface has been disposed";
	}
}

/** Registration token for one retained NativeKit frame callback. */
class NativeKitSurfaceFrameSubscription {
	final surface:NativeKitSurface;
	final callback:SurfaceFrameCallbackCallback;
	var disposed:Bool = false;

	@:allow(NativeKitSurface)
	private function new(surface:NativeKitSurface, callback:SurfaceFrameCallbackCallback) {
		this.surface = surface;
		this.callback = callback;
	}

	public function dispose():Void {
		if (disposed)
			return;
		surface.detach(this);
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(NativeKitSurface)
	function releasedAfterDetach():Void {
		if (disposed)
			return;
		disposed = true;
		callback.close();
	}

	@:allow(NativeKitSurface)
	function releasedAfterRuntimeShutdown():Void
		releasedAfterDetach();
}

/** Adapts NativeKit's full ABI callback to the ergonomic size-only surface callback. */
private class NativeKitSurfaceFrameHandler {
	final handler:Int->Int->Void;

	public function new(handler:Int->Int->Void)
		this.handler = handler;

	public function invoke(_surface:SurfaceHandle, width:Int, height:Int, _userData:hl.Abstract<"native_pointer">):Void
		handler(width, height);
}
