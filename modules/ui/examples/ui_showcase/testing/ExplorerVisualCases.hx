package testing;

import UiExplorer;
import testing.ExplorerVisualCase;

/** Deterministic screenshot setup, keeping numeric IDs at the host boundary. */
class ExplorerVisualCases {
	public static function apply(explorer:UiExplorer, caseId:Int):Bool {
		var state = explorer.state;
		state.overlays.dialogOpen = false;
		state.overlays.popupOpen = false;
		state.overlays.menuOpen = false;
		state.searchText = "";
		state.inspector.open = true;
		state.inspector.tab = "preview";
		state.inspector.selectedNodeId = 0;
		state.inspector.hoveredNodeId = 0;
		state.lightTheme = false;
		explorer.context.setTheme(UiExplorer.makeTheme(false));
		state.controls.checked = true;
		state.controls.enabled = true;
		state.controls.volume = 0.68;
		state.controls.progress = 0.72;
		state.controls.spinnerRunning = true;
		state.controls.radioValue = "comfortable";
		state.controls.selectValue = "comfortable";
		state.controls.comboValue = "comfortable";
		state.controls.selectedTab = "preview";
		state.controls.nameValue = "NativeKit UI";
		state.controls.notesValue = "مرحبا NativeKit — שלום — こんにちは 👋";
		state.textArabicValue = "مرحبا بالعالم";
		state.textHebrewValue = "שלום עולם";
		state.textJapaneseValue = "こんにちは世界";
		state.textEmojiValue = "NativeKit 👋 🌍 ✨";
		state.controls.menuSelection = "No command selected";
		state.listController.jumpTo(0.0, 0.0);
		state.visualFocusLabel = null;
		state.visualTextAreaSelection = false;
		state.visualTextComposition = false;
		state.selectedPage = "overview";
		switch caseId {
			case ExplorerVisualCase.Overview, ExplorerVisualCase.OverviewCompact:
				state.selectedPage = "overview";
			case ExplorerVisualCase.Controls: state.selectedPage = "controls";
			case ExplorerVisualCase.ControlsLight:
				state.selectedPage = "controls";
				state.lightTheme = true;
				explorer.context.setTheme(UiExplorer.makeTheme(true));
			case ExplorerVisualCase.ControlsFocused:
				state.selectedPage = "controls";
				state.visualFocusLabel = "Primary action";
			case ExplorerVisualCase.TextFocused:
				state.selectedPage = "text";
				state.visualFocusLabel = "Display name";
			case ExplorerVisualCase.Layout, ExplorerVisualCase.LayoutCompact:
				state.selectedPage = "layout";
			case ExplorerVisualCase.ListsScrolled, ExplorerVisualCase.ListsCompact:
				state.selectedPage = "lists";
				if (caseId == ExplorerVisualCase.ListsScrolled)
					state.listController.jumpTo(0.0, 414.0 * UiExplorer.LIST_ROW_HEIGHT);
			case ExplorerVisualCase.Dialog:
				state.selectedPage = "overlays";
				state.overlays.dialogOpen = true;
			case ExplorerVisualCase.Popup:
				state.selectedPage = "overlays";
				state.overlays.popupOpen = true;
			case ExplorerVisualCase.Menu:
				state.selectedPage = "overlays";
				state.overlays.menuOpen = true;
			case ExplorerVisualCase.ControlsCompact: state.selectedPage = "controls";
			case ExplorerVisualCase.Gestures, ExplorerVisualCase.GesturesCompact:
				state.selectedPage = "gestures";
			case ExplorerVisualCase.ListsLight:
				state.selectedPage = "lists";
				state.lightTheme = true;
				explorer.context.setTheme(UiExplorer.makeTheme(true));
			case ExplorerVisualCase.TextAreaSelection:
				state.selectedPage = "text";
				state.visualFocusLabel = "Multilingual notes";
				state.visualTextAreaSelection = true;
			case ExplorerVisualCase.TextComposition:
				state.selectedPage = "text";
				state.visualFocusLabel = "Multilingual notes";
				state.visualTextComposition = true;
			case ExplorerVisualCase.MenuLight:
				state.selectedPage = "overlays";
				state.lightTheme = true;
				explorer.context.setTheme(UiExplorer.makeTheme(true));
				state.overlays.menuOpen = true;
			case ExplorerVisualCase.TextCompact: state.selectedPage = "text";
			case ExplorerVisualCase.OverlaysCompact: state.selectedPage = "overlays";
			case ExplorerVisualCase.Graphics, ExplorerVisualCase.GraphicsCompact:
				state.selectedPage = "graphics";
			default: return false;
		}
		return true;
	}
}
