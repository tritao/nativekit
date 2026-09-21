package nativekit.ui.core;

/** Value snapshot exchanged between property descriptors and editors. */
enum PropertyValue {
	Unavailable;
	Mixed;
	Bool(value:Bool);
	Int(value:Int);
	Float(value:Float);
	Text(value:String);
	Enum(value:String);
}
