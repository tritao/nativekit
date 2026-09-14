package nativekit.ui.semantics;

/** Roles already understood by NativeKit's virtual accessibility tree. */
enum abstract AccessibilityRole(Int) from Int to Int {
	var Group = 0;
	var Button = 1;
	var Checkbox = 2;
	var Radio = 3;
	var Text = 4;
	var TextField = 5;
	var Link = 6;
	var Image = 7;
	var Heading = 8;
	var List = 9;
	var ListItem = 10;
	var Slider = 11;
	var ScrollArea = 12;
	var Dialog = 13;
	var Menu = 14;
	var MenuBar = 15;
	var MenuItem = 16;
	var TabList = 17;
	var Tab = 18;
	var TabPanel = 19;
	var Switch = 20;
	var ProgressBar = 21;
	var ComboBox = 22;
	var Collection = 23;
	var CollectionItem = 24;
	var Grid = 25;
	var Row = 26;
	var Cell = 27;
	var ColumnHeader = 28;
	var RowHeader = 29;
	var Tree = 30;
	var TreeItem = 31;
	var Separator = 32;
	var Toolbar = 33;
	var Status = 34;
	var Alert = 35;
}
