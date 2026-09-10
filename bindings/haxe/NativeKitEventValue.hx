enum NativeKitEventValue {
	None;
	ClipboardText(request:haxe.Int64, result:Int, text:String);
	ClipboardFiles(request:haxe.Int64, result:Int, paths:Array<String>);
	DropText(source:Int, text:String);
	DropFiles(source:Int, paths:Array<String>);
	DialogPaths(request:haxe.Int64, result:Int, accepted:Bool, paths:Array<String>);
	DialogMessage(request:haxe.Int64, result:Int, button:Int);
	WebViewNavigated(source:Int, url:String);
	WebViewMessage(source:Int, json:String);
	WebViewTitleChanged(source:Int, title:String);
	WebViewEvaluation(source:Int, request:haxe.Int64, result:Int, json:String);
	WebViewNavigationFailed(source:Int, category:Int, message:String);
	WebViewNavigationRequest(source:Int, request:haxe.Int64, url:String);
	NotificationActivated(request:haxe.Int64, action:String);
	NotificationFailed(request:haxe.Int64, message:String);
	Raw(kind:Int, source:Int, request:haxe.Int64, result:Int, flags:Int, dataCount:Int, data:haxe.io.Bytes);
	WindowClose(source:Int);
	WindowResize(source:Int, width:Int, height:Int);
	WindowMove(source:Int, x:Int, y:Int);
	WindowFramebufferResize(source:Int, width:Int, height:Int);
	WindowScaleChanged(source:Int, scale:Float);
	WindowStateChanged(source:Int, stateFlags:Int);
	Key(source:Int, key:Int, scancode:Int, action:Int, modifiers:Int);
	TextInput(source:Int, codepoint:Int);
	TextEdit(source:Int, edit:NativeKitTextEdit);
	PointerMove(source:Int, x:Float, y:Float);
	PointerButton(source:Int, button:Int, action:Int, modifiers:Int, x:Float, y:Float);
	PointerScroll(source:Int, x:Float, y:Float);
	PointerEnter(source:Int, entered:Bool);
	Touch(source:Int, pointerId:Int, action:Int, tool:Int, modifiers:Int, x:Float, y:Float, pressure:Float, tiltX:Float, tiltY:Float);
	JoystickAxis(source:Int, axis:Int, value:Float);
	JoystickButton(source:Int, button:Int, pressed:Bool);
	JoystickHat(source:Int, hat:Int, value:Int);
	GamepadAxis(source:Int, axis:Int, value:Float);
	GamepadButton(source:Int, button:Int, pressed:Bool);
	SurfaceReady(source:Int);
	SurfaceResize(source:Int, width:Int, height:Int, framebufferWidth:Int, framebufferHeight:Int);
	SurfaceLost(source:Int);
	Resources(kind:Int, request:haxe.Int64, result:Int, accepted:Bool, items:Array<NativeKitResource>);
	ShareReceived(text:Null<String>, subject:Null<String>, items:Array<NativeKitResource>);
	ResourceDrop(source:Int, x:Float, y:Float, text:Null<String>, items:Array<NativeKitResource>);
}

class NativeKitTextEdit {
	public final action:Int;
	public final text:Null<String>;
	public final replaceStart:Int;
	public final replaceEnd:Int;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final compositionStart:Int;
	public final compositionEnd:Int;

	public function new(action:Int, text:Null<String>, replaceStart:Int, replaceEnd:Int,
		selectionStart:Int, selectionEnd:Int, compositionStart:Int, compositionEnd:Int) {
		this.action = action; this.text = text;
		this.replaceStart = replaceStart; this.replaceEnd = replaceEnd;
		this.selectionStart = selectionStart; this.selectionEnd = selectionEnd;
		this.compositionStart = compositionStart; this.compositionEnd = compositionEnd;
	}
}

class NativeKitResource {
	public final flags:Int;
	public final uri:String;
	public final mimeType:Null<String>;
	public final displayName:Null<String>;
	public function new(flags:Int, uri:String, mimeType:Null<String>, displayName:Null<String>) {
		this.flags = flags;
		this.uri = uri;
		this.mimeType = mimeType;
		this.displayName = displayName;
	}
}
