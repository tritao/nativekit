package nativekit.ui.core;

/** Controls whether a render node contributes itself, its descendants, or neither to hit testing. */
enum HitTestBehavior {
	/** Use the node's legacy hitTestSelf flag and traverse children. */
	Auto;
	/** Exclude this node and its entire subtree from hit testing. */
	None;
	/** Hit-test this node but do not traverse its children. */
	SelfOnly;
	/** Traverse children but never hit-test this node's own bounds. */
	ChildrenOnly;
}
