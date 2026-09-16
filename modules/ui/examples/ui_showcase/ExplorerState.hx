import nativekit.ui.widgets.ScrollController;
import state.ControlsDemoState;
import state.GestureDemoState;
import state.InspectorState;
import state.OverlayState;
import nativekit.ui.widgets.TextEditorDiagnostics;

/** Typed, persistent showcase state, separate from runtime/render ownership. */
class ExplorerState {
	public final inspector:InspectorState;
	public final controls:ControlsDemoState;
	public final gestures:GestureDemoState;
	public final overlays:OverlayState;
	public final listController:ScrollController;
	public var lightTheme:Bool = false;
	public var selectedPage:String = "overview";
	public var searchText:String = "";
	public var smokeFocusTextField:Bool = false;
	public var smokeSelectManaged:Bool = false;
	public var smokeOpenSelect:Bool = false;
	public var visualFocusLabel:Null<String> = null;
	public var visualTextAreaSelection:Bool = false;
	public var visualTextComposition:Bool = false;
	public var textDiagnostics:Null<TextEditorDiagnostics> = null;
	public var textLastClipboardAction:String = "No clipboard command requested";
	public var textLastSubmit:String = "No editor submitted yet";
	public var textArabicValue:String = "مرحبا بالعالم";
	public var textHebrewValue:String = "שלום עולם";
	public var textJapaneseValue:String = "こんにちは世界";
	public var textEmojiValue:String = "NativeKit 👋 🌍 ✨";

	public function new() {
		inspector = new InspectorState();
		controls = new ControlsDemoState();
		gestures = new GestureDemoState();
		overlays = new OverlayState();
		listController = new ScrollController();
	}
}
