package shell;

import LayoutAxis;
import LayoutStyle;
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
			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.grow();
			contentStyle.height = LayoutAxis.fit();
			contentStyle.childGap = 12.0;
			var dialog = new Dialog("showcase-dialog", "NativeKit dialog",
				new Column("dialog-content", [
					explorer.keyed("copy", explorer.caption(
						"A modal overlay rendered in the same resolved UI tree.")),
					explorer.keyed("close", explorer.button("Done", "dialog-done", function() {
						explorer.state.overlays.dialogOpen = false;
					}))
				], contentStyle), function() {
					explorer.state.overlays.dialogOpen = false;
				}, 390.0);
			layers.push(new StackChild("dialog-layer", dialog, 0.0, 0.0, 30));
		} else if (explorer.state.overlays.popupOpen) {
			var popupWidth = 266.0;
			var popupHeight = 164.0;
			var edge = 8.0;
			var anchorX = explorer.state.overlays.popupAnchorX;
			var anchorTop = explorer.state.overlays.popupAnchorTop;
			var anchorBottom = explorer.state.overlays.popupAnchorBottom;
			if (anchorX < 0.0 || anchorTop < 0.0 || anchorBottom < anchorTop) {
				anchorX = Math.max(270.0, explorer.width * 0.42);
				anchorTop = 142.0;
				anchorBottom = 142.0;
			}
			var popupX = clamp(anchorX, edge,
				Math.max(edge, explorer.width - edge - popupWidth));
			var belowY = anchorBottom + 6.0;
			var popupY = belowY + popupHeight <= explorer.height - edge
				? belowY : Math.max(edge, anchorTop - 6.0 - popupHeight);
			var popupContent = new Column("popup-content", [
				explorer.keyed("title", explorer.heading("Quick actions")),
				explorer.keyed("copy", explorer.caption("This popup escapes the page clip.")),
				explorer.keyed("dismiss", explorer.button("Close popup", "popup-close", function() {
					explorer.state.overlays.popupOpen = false;
				}))
			], explorer.panelStyle(250.0));
			var popup = new Popup("showcase-popup", popupContent,
				popupX, popupY, null, function() {
					explorer.state.overlays.popupOpen = false;
				});
			popup.label = "Quick actions";
			popup.modal = false;
			layers.push(new StackChild("popup-layer", popup, 0.0, 0.0, 20));
		} else if (explorer.state.overlays.menuOpen) {
			var menuWidth = 236.0;
			var menuHeight = 132.0;
			var edge = 8.0;
			var anchorX = explorer.state.overlays.menuAnchorX;
			var anchorTop = explorer.state.overlays.menuAnchorTop;
			var anchorBottom = explorer.state.overlays.menuAnchorBottom;
			if (anchorX < 0.0 || anchorTop < 0.0 || anchorBottom < anchorTop) {
				anchorX = 330.0;
				anchorTop = 159.0;
				anchorBottom = 159.0;
			}
			var menuX = clamp(anchorX, edge,
				Math.max(edge, explorer.width - edge - menuWidth));
			var belowY = anchorBottom + 6.0;
			var menuY = belowY + menuHeight <= explorer.height - edge
				? belowY : Math.max(edge, anchorTop - 6.0 - menuHeight);
			var menu = new Menu("showcase-menu", [
				new MenuItem("menu-new", "New document", function() {
					explorer.state.controls.menuSelection = "New document";
				}),
				new MenuItem("menu-copy", "Copy selection", function() {
					explorer.state.controls.menuSelection = "Copy selection";
				}),
				new MenuItem("menu-disabled", "Unavailable action", null, false)
			], menuX, menuY, function() {
				explorer.state.overlays.menuOpen = false;
			});
			layers.push(new StackChild("menu-layer", menu, 0.0, 0.0, 20));
		}
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return Math.max(minimum, Math.min(maximum, value));
}
