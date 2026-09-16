package testing;

import ExplorerState;
import testing.ExplorerSmokeFrame;

/** Deterministic page/overlay setup for native smoke-test frames. */
class ExplorerSmokeSequence {
	public static inline var FRAME_COUNT:Int = 13;

	public static function apply(state:ExplorerState, frame:Int):Void {
		state.overlays.dialogOpen = false;
		state.overlays.popupOpen = false;
		state.overlays.menuOpen = false;
		state.inspector.open = true;
		state.smokeFocusTextField = false;
		state.smokeSelectManaged = true;
		state.smokeOpenSelect = false;
		switch frame % FRAME_COUNT {
			case ExplorerSmokeFrame.Overview: state.selectedPage = "overview";
			case ExplorerSmokeFrame.Controls: state.selectedPage = "controls";
			case ExplorerSmokeFrame.Text: state.selectedPage = "text";
			case ExplorerSmokeFrame.FocusedText:
				state.selectedPage = "text";
				state.smokeFocusTextField = true;
			case ExplorerSmokeFrame.Layout: state.selectedPage = "layout";
			case ExplorerSmokeFrame.Lists: state.selectedPage = "lists";
			case ExplorerSmokeFrame.Overlays: state.selectedPage = "overlays";
			case ExplorerSmokeFrame.Dialog:
				state.selectedPage = "overlays";
				state.overlays.dialogOpen = true;
			case ExplorerSmokeFrame.Popup:
				state.selectedPage = "overlays";
				state.overlays.popupOpen = true;
			case ExplorerSmokeFrame.Menu:
				state.selectedPage = "overlays";
				state.overlays.menuOpen = true;
			case ExplorerSmokeFrame.Graphics: state.selectedPage = "graphics";
			case ExplorerSmokeFrame.Gestures: state.selectedPage = "gestures";
			case ExplorerSmokeFrame.Select:
				state.selectedPage = "controls";
				state.smokeOpenSelect = true;
			default: state.selectedPage = "overview";
		}
	}
}
