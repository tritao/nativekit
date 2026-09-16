package nativekit.audio;

import NativeKit;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEvents.NativeKitEventSubscription;
import nativekit.resource.Resource;
import nativekit.resource.ResourceCache;
import nativekit.resource.ResourceCache.ResourceCacheLoad;

/**
 * Named audio resource catalog built on NativeKit's general ResourceCache.
 *
 * The bank borrows the cache and event pump, but owns each asset view. Clips
 * created from an entry are independent caller-owned audio resources and may
 * outlive the bank entry because NativeKit audio retains its source bytes.
 */
class AudioBank {
	final cache:ResourceCache;
	final events:NativeKitEvents;
	final entries:Array<AudioBankEntry> = [];
	final subscription:NativeKitEventSubscription;
	var disposed:Bool = false;

	/** Called once when an async or synchronous entry reaches the ready state. */
	public var onReady:AudioBankEntry->Void = null;
	/** Called once when an async entry fails to load. */
	public var onFailed:AudioBankEntry->NativeKit.Result->Void = null;

	public function new(cache:ResourceCache, events:NativeKitEvents) {
		if (cache == null)
			throw "Audio bank resource cache must not be null";
		if (cache.isDisposed())
			throw "Audio bank resource cache must be live";
		if (events == null)
			throw "Audio bank event pump must not be null";
		this.cache = cache;
		this.events = events;
		this.subscription = events.listen(onEvent);
	}

	/** Loads and registers a ready entry synchronously. */
	public function load(name:String, resource:Resource):AudioBankEntry {
		ensureLive();
		validateName(name);
		validateResource(resource);
		ensureNameAvailable(name);
		var asset = cache.load(resource);
		var entry = new AudioBankEntry(name, resource, asset, haxe.Int64.ofInt(0));
		entries.push(entry);
		notifyState(entry);
		return entry;
	}

	/** Starts an async complete-resource load and registers its named entry. */
	public function loadAsync(name:String, resource:Resource):AudioBankEntry {
		ensureLive();
		validateName(name);
		validateResource(resource);
		ensureNameAvailable(name);
		var loaded:ResourceCacheLoad = cache.loadAsync(resource);
		var entry = new AudioBankEntry(name, resource, loaded.asset, loaded.request);
		entries.push(entry);
		// A cache hit may already be ready and deliberately emits no event.
		notifyState(entry);
		return entry;
	}

	/** Returns a named entry, or null when it has not been registered. */
	public function find(name:String):Null<AudioBankEntry> {
		ensureLive();
		if (name == null || name.length == 0)
			return null;
		for (entry in entries)
			if (entry.name == name)
				return entry;
		return null;
	}

	/** Returns a named entry or throws when the bank does not contain it. */
	public function get(name:String):AudioBankEntry {
		var entry = find(name);
		if (entry == null)
			throw "Audio bank entry was not found: " + name;
		return entry;
	}

	/** Creates a clip from a ready named entry. */
	public function createClip(name:String):Clip
		return get(name).createClip();

	public function contains(name:String):Bool
		return find(name) != null;

	/** Returns the number of registered entries. */
	public function count():Int {
		ensureLive();
		return entries.length;
	}

	/** Unloads one entry; clips created from it retain their audio source. */
	public function unload(name:String):Bool {
		ensureLive();
		var index = indexOf(name);
		if (index < 0)
			return false;
		entries[index].dispose();
		entries.splice(index, 1);
		return true;
	}

	/** Unloads every registered entry. */
	public function clear():Void {
		ensureLive();
		var failure:Dynamic = null;
		while (entries.length > 0) {
			var entry = entries.pop();
			try {
				entry.dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		if (failure != null)
			throw failure;
	}

	/** Releases all entries and the bank's event subscription. */
	public function dispose():Void {
		if (disposed)
			return;
		var failure:Dynamic = null;
		while (entries.length > 0) {
			var entry = entries.pop();
			try {
				entry.dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		if (failure != null)
			throw failure;
		subscription.dispose();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function onEvent(value:NativeKitEventValue):Void {
		switch value {
			case ResourceAssetReady(source, request): notify(source, request);
			case ResourceAssetLoadFailed(source, request, _): notify(source, request);
			case _: return;
		}
	}

	function notify(source:NativeKit.Handle, request:haxe.Int64):Void {
		for (entry in entries) {
			if (!entry.matches(source, request))
				continue;
			notifyState(entry);
			return;
		}
	}

	function notifyState(entry:AudioBankEntry):Void {
		switch entry.loadState() {
			case NativeKit.ResourceAssetLoadState.Ready:
				if (entry.markReadyNotified() && onReady != null)
					onReady(entry);
			case NativeKit.ResourceAssetLoadState.LoadFailed:
				if (entry.markFailedNotified() && onFailed != null)
					onFailed(entry, entry.loadResult());
			case NativeKit.ResourceAssetLoadState.Loading:
		}
	}

	function indexOf(name:String):Int {
		for (index in 0...entries.length)
			if (entries[index].name == name)
				return index;
		return -1;
	}

	function ensureNameAvailable(name:String):Void {
		if (indexOf(name) >= 0)
			throw "Audio bank entry name is already registered: " + name;
	}

	static function validateName(name:String):Void {
		if (name == null || name.length == 0)
			throw "Audio bank entry name must not be empty";
	}

	static function validateResource(resource:Resource):Void {
		if (resource == null)
			throw "Audio bank resource must not be null";
	}

	function ensureLive():Void {
		if (disposed)
			throw "Audio bank has been disposed";
	}
}
