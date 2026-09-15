enum NativeKitEventValue {
	None;
	ClipboardText(request:haxe.Int64, result:NativeKit.Result, text:String);
	ClipboardFiles(request:haxe.Int64, result:NativeKit.Result, paths:Array<String>);
	DropText(source:NativeKit.Handle, text:String);
	DropFiles(source:NativeKit.Handle, paths:Array<String>);
	DialogMessage(request:haxe.Int64, result:NativeKit.Result, button:NativeKit.MessageResult);
	WebViewNavigated(source:NativeKit.Handle, url:String);
	WebViewMessage(source:NativeKit.Handle, json:String);
	WebViewTitleChanged(source:NativeKit.Handle, title:String);
	WebViewEvaluation(source:NativeKit.Handle, request:haxe.Int64, result:NativeKit.Result, json:String);
	WebViewNavigationFailed(source:NativeKit.Handle, category:NativeKit.NavigationError, message:String);
	WebViewNavigationRequest(source:NativeKit.Handle, request:haxe.Int64, url:String);
	NotificationActivated(request:haxe.Int64, action:String);
	NotificationFailed(request:haxe.Int64, message:String);
	TaskProgress(source:NativeKit.Handle, data:haxe.io.Bytes);
	TaskComplete(source:NativeKit.Handle, result:NativeKit.Result, data:haxe.io.Bytes);
	TaskFailed(source:NativeKit.Handle, result:NativeKit.Result, data:haxe.io.Bytes);
	TaskCancelled(source:NativeKit.Handle, result:NativeKit.Result, data:haxe.io.Bytes);
	Raw(kind:NativeKit.EventKind, source:NativeKit.Handle, request:haxe.Int64, result:NativeKit.Result,
		flags:Int, dataCount:Int, data:haxe.io.Bytes);
	WindowClose(source:NativeKit.Handle);
	WindowResize(source:NativeKit.Handle, width:Int, height:Int);
	WindowMove(source:NativeKit.Handle, x:Int, y:Int);
	WindowFramebufferResize(source:NativeKit.Handle, width:Int, height:Int);
	WindowScaleChanged(source:NativeKit.Handle, scale:Float);
	WindowStateChanged(source:NativeKit.Handle, stateFlags:NativeKit.WindowStateFlags);
	Key(source:NativeKit.Handle, key:NativeKit.Key, scancode:Int, action:NativeKit.InputAction, modifiers:NativeKit.Modifiers);
	TextInput(source:NativeKit.Handle, codepoint:Int);
	TextEdit(source:NativeKit.Handle, edit:NativeKitTextEdit);
	AccessibilityAction(source:NativeKit.Handle, nodeId:Int, action:Int, value:Null<String>,
		selectionStart:Int, selectionEnd:Int, granularity:Int);
	PointerMove(source:NativeKit.Handle, x:Float, y:Float);
	PointerButton(source:NativeKit.Handle, button:NativeKit.PointerButton, action:NativeKit.InputAction,
		modifiers:NativeKit.Modifiers, x:Float, y:Float);
	PointerScroll(source:NativeKit.Handle, x:Float, y:Float);
	PointerEnter(source:NativeKit.Handle, entered:Bool);
	Touch(source:NativeKit.Handle, pointerId:Int, action:NativeKit.TouchAction, tool:NativeKit.TouchTool,
		modifiers:NativeKit.Modifiers, x:Float, y:Float, pressure:Float, tiltX:Float, tiltY:Float);
	JoystickAxis(source:NativeKit.Handle, axis:Int, value:Float);
	JoystickButton(source:NativeKit.Handle, button:Int, pressed:Bool);
	JoystickHat(source:NativeKit.Handle, hat:Int, value:NativeKit.JoystickHatFlags);
	GamepadAxis(source:NativeKit.Handle, axis:NativeKit.GamepadAxis, value:Float);
	GamepadButton(source:NativeKit.Handle, button:NativeKit.GamepadButton, pressed:Bool);
	SurfaceReady(source:NativeKit.Handle);
	SurfaceResize(source:NativeKit.Handle, width:Int, height:Int, framebufferWidth:Int, framebufferHeight:Int);
	SurfaceLost(source:NativeKit.Handle);
	Resources(kind:NativeKit.EventKind, request:haxe.Int64, result:NativeKit.Result, accepted:Bool, items:Array<NativeKitResource>);
	ShareReceived(text:Null<String>, subject:Null<String>, items:Array<NativeKitResource>);
	ResourceDrop(source:NativeKit.Handle, x:Float, y:Float, text:Null<String>, items:Array<NativeKitResource>);
	AudioVoiceReady(source:NativeKit.Handle);
	AudioVoiceLoadFailed(source:NativeKit.Handle, result:NativeKit.Result);
	AudioVoiceComplete(source:NativeKit.Handle);
}

class NativeKitTextEdit {
	public final action:NativeKit.TextEditAction;
	public final text:Null<String>;
	public final replaceStart:Int;
	public final replaceEnd:Int;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final compositionStart:Int;
	public final compositionEnd:Int;

	public function new(action:NativeKit.TextEditAction, text:Null<String>, replaceStart:Int, replaceEnd:Int,
		selectionStart:Int, selectionEnd:Int, compositionStart:Int, compositionEnd:Int) {
		this.action = action; this.text = text;
		this.replaceStart = replaceStart; this.replaceEnd = replaceEnd;
		this.selectionStart = selectionStart; this.selectionEnd = selectionEnd;
		this.compositionStart = compositionStart; this.compositionEnd = compositionEnd;
	}
}

class NativeKitResource {
	public final flags:NativeKit.ResourceFlags;
	public final uri:String;
	public final mimeType:Null<String>;
	public final displayName:Null<String>;
	public function new(flags:NativeKit.ResourceFlags, uri:String, mimeType:Null<String>, displayName:Null<String>) {
		this.flags = flags;
		this.uri = uri;
		this.mimeType = mimeType;
		this.displayName = displayName;
	}
}
