import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;
enum NativeKitEventValue {
	None;
	ClipboardText(request:haxe.Int64, result:Result, text:String);
	ClipboardFiles(request:haxe.Int64, result:Result, paths:Array<String>);
	DropText(source:Handle, text:String);
	DropFiles(source:Handle, paths:Array<String>);
	DialogMessage(request:haxe.Int64, result:Result, button:MessageResult);
	WebViewNavigated(source:Handle, url:String);
	WebViewMessage(source:Handle, json:String);
	WebViewTitleChanged(source:Handle, title:String);
	WebViewEvaluation(source:Handle, request:haxe.Int64, result:Result, json:String);
	WebViewNavigationFailed(source:Handle, category:NavigationError, message:String);
	WebViewNavigationRequest(source:Handle, request:haxe.Int64, url:String);
	NotificationActivated(request:haxe.Int64, action:String);
	NotificationFailed(request:haxe.Int64, message:String);
	TaskProgress(source:Handle, data:haxe.io.Bytes);
	TaskComplete(source:Handle, result:Result, data:haxe.io.Bytes);
	TaskFailed(source:Handle, result:Result, data:haxe.io.Bytes);
	TaskCancelled(source:Handle, result:Result, data:haxe.io.Bytes);
	Raw(kind:EventKind, source:Handle, request:haxe.Int64, result:Result,
		flags:Int, dataCount:Int, data:haxe.io.Bytes);
	WindowClose(source:Handle);
	WindowResize(source:Handle, width:Int, height:Int);
	WindowMove(source:Handle, x:Int, y:Int);
	WindowFramebufferResize(source:Handle, width:Int, height:Int);
	WindowScaleChanged(source:Handle, scale:Float);
	WindowStateChanged(source:Handle, stateFlags:WindowStateFlags);
	Key(source:Handle, key:Key, scancode:Int, action:InputAction, modifiers:Modifiers);
	TextInput(source:Handle, codepoint:Int);
	TextEdit(source:Handle, edit:NativeKitTextEdit);
	AccessibilityAction(source:Handle, nodeId:Int, action:Int, value:Null<String>,
		selectionStart:Int, selectionEnd:Int, granularity:Int);
	PointerMove(source:Handle, x:Float, y:Float);
	PointerButton(source:Handle, button:PointerButton, action:InputAction,
		modifiers:Modifiers, x:Float, y:Float);
	PointerScroll(source:Handle, x:Float, y:Float);
	PointerEnter(source:Handle, entered:Bool);
	Touch(source:Handle, pointerId:Int, action:TouchAction, tool:TouchTool,
		modifiers:Modifiers, x:Float, y:Float, pressure:Float, tiltX:Float, tiltY:Float);
	JoystickAxis(source:Handle, axis:Int, value:Float);
	JoystickButton(source:Handle, button:Int, pressed:Bool);
	JoystickHat(source:Handle, hat:Int, value:JoystickHatFlags);
	GamepadAxis(source:Handle, axis:GamepadAxis, value:Float);
	GamepadButton(source:Handle, button:GamepadButton, pressed:Bool);
	SurfaceReady(source:Handle);
	SurfaceResize(source:Handle, width:Int, height:Int, framebufferWidth:Int, framebufferHeight:Int);
	SurfaceLost(source:Handle);
	Resources(kind:EventKind, request:haxe.Int64, result:Result, accepted:Bool, items:Array<NativeKitResource>);
	ShareReceived(text:Null<String>, subject:Null<String>, items:Array<NativeKitResource>);
	ResourceDrop(source:Handle, x:Float, y:Float, text:Null<String>, items:Array<NativeKitResource>);
	ResourceCommit(request:haxe.Int64, result:Result, downloadInitiated:Bool);
}

class NativeKitTextEdit {
	public final action:TextEditAction;
	public final text:Null<String>;
	public final replaceStart:Int;
	public final replaceEnd:Int;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final compositionStart:Int;
	public final compositionEnd:Int;
	public final selectionAffinity:Int;
	public final historyKind:Int;

	public function new(action:TextEditAction, text:Null<String>, replaceStart:Int, replaceEnd:Int,
		selectionStart:Int, selectionEnd:Int, compositionStart:Int, compositionEnd:Int,
		?selectionAffinity:Int = 0, ?historyKind:Int = 0) {
		this.action = action; this.text = text;
		this.replaceStart = replaceStart; this.replaceEnd = replaceEnd;
		this.selectionStart = selectionStart; this.selectionEnd = selectionEnd;
		this.compositionStart = compositionStart; this.compositionEnd = compositionEnd;
		this.selectionAffinity = selectionAffinity;
		this.historyKind = historyKind;
	}
}

class NativeKitResource {
	public final flags:ResourceFlags;
	public final uri:String;
	public final mimeType:Null<String>;
	public final displayName:Null<String>;
	public function new(flags:ResourceFlags, uri:String, mimeType:Null<String>, displayName:Null<String>) {
		this.flags = flags;
		this.uri = uri;
		this.mimeType = mimeType;
		this.displayName = displayName;
	}
}
