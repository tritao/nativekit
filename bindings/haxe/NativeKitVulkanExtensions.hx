import NativeKit.Result;
import NativeKitWindow;

/** Safe Haxe view of NativeKit's process-lifetime Vulkan extension names. */
class NativeKitVulkanExtensions {
	/** Queries and copies the instance extensions required by a window's presentation backend. */
	public static function requiredInstanceExtensions(window:NativeKitWindow):Array<String> {
		var result = NativeKitVulkan.getRequiredInstanceExtensions(window.nativeHandle());
		if (result.status != Result.Ok)
			throw 'NativeKit Vulkan extension query failed: ${NativeKit.nk_last_error()}';
		var extensions:Array<String> = [];
		for (extension in result.extensions) {
			if (extension == null)
				throw "NativeKit returned a null Vulkan extension name";
			extensions.push(extension);
		}
		return extensions;
	}
}
