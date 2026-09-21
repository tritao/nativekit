package nativekit.ui.core;

/** Result of validating and applying one property binding edit. */
enum PropertyEditResult {
	/** The value was written and an undoable operation was recorded. */
	Applied;
	/** The requested value was already present, so no history entry was made. */
	Unchanged;
	/** The edit was rejected before it changed the application model. */
	Rejected(message:String);
}
