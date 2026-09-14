package shell;

import UiExplorer;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Menu;
import nativekit.ui.widgets.MenuItem;
import nativekit.ui.widgets.Popup;
import nativekit.ui.widgets.StackChild;

/** Builds global popup, menu and dialog layers above the Explorer shell. */
class OverlayHost {
	public static function appendLayers(explorer:UiExplorer,
			layers:Array<StackChild>):Void {
		if (explorer.state.overlays.dialogOpen) {
			var dialog = new Dialog("showcase-dialog", "NativeKit dialog",
				new Column("dialog-content", [
					explorer.keyed("copy", explorer.text(
						"A modal overlay rendered in the same resolved UI tree.",
						explorer.paletteMuted())),
					explorer.keyed("close", explorer.button("Done", "dialog-done", function() {
						explorer.state.overlays.dialogOpen = false;
					}))
				], explorer.columnStyle(340.0, 90.0)), function() {
					explorer.state.overlays.dialogOpen = false;
				}, 390.0);
			layers.push(new StackChild("dialog-layer", dialog, 0.0, 0.0, 30));
		} else if (explorer.state.overlays.popupOpen) {
			var popupContent = new Column("popup-content", [
				explorer.keyed("title", explorer.text("Quick actions", explorer.paletteText())),
				explorer.keyed("copy", explorer.text("This popup escapes the page clip.",
					explorer.paletteMuted())),
				explorer.keyed("dismiss", explorer.button("Close popup", "popup-close", function() {
					explorer.state.overlays.popupOpen = false;
				}))
			], explorer.panelStyle(250.0));
			var popup = new Popup("showcase-popup", popupContent,
				Math.max(270.0, explorer.width * 0.42), 150.0, null, function() {
					explorer.state.overlays.popupOpen = false;
				});
			popup.label = "Quick actions";
			popup.modal = false;
			layers.push(new StackChild("popup-layer", popup, 0.0, 0.0, 20));
		} else if (explorer.state.overlays.menuOpen) {
			var menu = new Menu("showcase-menu", [
				new MenuItem("menu-new", "New document", function() {
					explorer.state.controls.menuSelection = "New document";
				}),
				new MenuItem("menu-copy", "Copy selection", function() {
					explorer.state.controls.menuSelection = "Copy selection";
				}),
				new MenuItem("menu-disabled", "Unavailable action", null, false)
			], 330.0, 165.0, function() {
				explorer.state.overlays.menuOpen = false;
			});
			layers.push(new StackChild("menu-layer", menu, 0.0, 0.0, 20));
		}
	}
}
