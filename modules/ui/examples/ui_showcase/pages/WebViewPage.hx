package pages;

import UiExplorer;
import components.WebViewSlot;
import nativekit.ui.widgets.KeyedView;

/** Native WebView embedded into Haxeon layout through a synchronized viewport slot. */
class WebViewPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "WebView",
			"Embed platform web content while Haxeon owns layout, navigation and surrounding UI.");
		if (!explorer.webViewAvailable()) {
			items.push(explorer.keyed("unsupported", explorer.panel("webview-unsupported", [
				explorer.keyed("heading", explorer.heading("WebView unavailable")),
				explorer.keyed("copy", explorer.caption(
					"This platform build does not advertise the native WebView capability."))
			])));
			return;
		}
		items.push(explorer.keyed("webview-info", explorer.panel("webview-info", [
			explorer.keyed("heading", explorer.heading("Native content, Haxeon layout")),
			explorer.keyed("copy", explorer.caption(
				"The viewport below follows resolved Haxeon bounds. NativeKit supplies the platform WebView dependency and lifecycle.")),
			explorer.keyed("reload", explorer.button("Reload demo", "reload-webview", function() {
				explorer.reloadWebView();
			}))
		])));
		items.push(explorer.keyed("webview-slot", new WebViewSlot("catalog-webview")));
	}
}
