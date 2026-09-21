package nativekit.ui.core;

/** Collects several already-applied edits into one undoable history entry. */
class EditTransaction {
	final history:EditHistory;
	public final label:String;
	public final coalesceKey:Null<String>;
	final operations:Array<EditOperation>;
	var closed:Bool;

	@:allow(nativekit.ui.core.EditHistory)
	private function new(history:EditHistory, label:String, coalesceKey:Null<String>) {
		this.history = history;
		this.label = label;
		this.coalesceKey = coalesceKey;
		operations = [];
		closed = false;
	}

	/** Applies and records an operation in this transaction. */
	public function apply(operation:EditOperation):Bool {
		ensureOpen();
		if (operation == null)
			throw "Edit transactions cannot apply null operations";
		operation.apply();
		operations.push(operation);
		return true;
	}

	/** Records a mutation that the caller has already applied. */
	public function record(operation:EditOperation):Bool {
		ensureOpen();
		if (operation == null)
			throw "Edit transactions cannot record null operations";
		operations.push(operation);
		return true;
	}

	/** Commits the collected mutations as one history entry. */
	public function commit():Bool {
		ensureOpen();
		closed = true;
		return history.commitTransaction(this);
	}

	/** Undoes all mutations and discards the transaction. */
	public function cancel():Bool {
		ensureOpen();
		closed = true;
		return history.cancelTransaction(this);
	}

	@:allow(nativekit.ui.core.EditHistory)
	function operationList():Array<EditOperation>
		return operations.copy();

	function ensureOpen():Void {
		if (closed)
			throw "Edit transaction is already closed";
	}
}
