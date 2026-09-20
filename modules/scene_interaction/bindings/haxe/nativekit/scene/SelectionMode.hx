package nativekit.scene;

/** Selection operation applied to one occurrence or pick result. */
enum abstract SelectionMode(Int) from Int to Int {
	var Replace = 0;
	var Add = 1;
	var Toggle = 2;
}
