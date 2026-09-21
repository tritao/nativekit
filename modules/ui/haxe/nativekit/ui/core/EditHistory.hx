package nativekit.ui.core;

/** Reversible editor history with transactions and drag/coalescing hooks. */
class EditHistory {
	final undoStack:Array<EditOperation>;
	final redoStack:Array<EditOperation>;
	var activeTransaction:Null<EditTransaction>;
	var nextStateToken:Int;
	public var stateToken(default, null):Int;
	public var revision(default, null):Int;

	public function new() {
		undoStack = [];
		redoStack = [];
		activeTransaction = null;
		nextStateToken = 1;
		stateToken = 0;
		revision = 0;
	}

	public var canUndo(get, never):Bool;
	inline function get_canUndo():Bool
		return undoStack.length > 0;

	public var canRedo(get, never):Bool;
	inline function get_canRedo():Bool
		return redoStack.length > 0;

	public var undoCount(get, never):Int;
	inline function get_undoCount():Int
		return undoStack.length;

	public var redoCount(get, never):Int;
	inline function get_redoCount():Int
		return redoStack.length;

	public function undoLabel():Null<String>
		return undoStack.length == 0 ? null : undoStack[undoStack.length - 1].label;

	public function redoLabel():Null<String>
		return redoStack.length == 0 ? null : redoStack[redoStack.length - 1].label;

	/** Applies an operation and adds it to the undo stack. */
	public function apply(operation:EditOperation, ?coalesceKey:String):Bool {
		ensureNoActiveTransaction();
		if (operation == null)
			throw "Edit history cannot apply null operations";
		operation.apply();
		pushApplied(operation, coalesceKey == null ? operation.coalesceKey : coalesceKey);
		return true;
	}

	/** Adds an operation that the caller has already applied. */
	public function record(operation:EditOperation, ?coalesceKey:String):Bool {
		ensureNoActiveTransaction();
		if (operation == null)
			throw "Edit history cannot record null operations";
		pushApplied(operation, coalesceKey == null ? operation.coalesceKey : coalesceKey);
		return true;
	}

	/** Begins a transaction for a compound edit or a continuous drag. */
	public function begin(label:String, ?coalesceKey:String):EditTransaction {
		if (label == null || label.length == 0)
			throw "Edit transactions require a label";
		if (coalesceKey != null && coalesceKey.length == 0)
			throw "Edit transaction coalescing keys cannot be empty";
		if (activeTransaction != null)
			throw "Edit history does not support nested transactions";
		var result = new EditTransaction(this, label, coalesceKey);
		activeTransaction = result;
		return result;
	}

	public function undo():Bool {
		ensureNoActiveTransaction();
		if (undoStack.length == 0)
			return false;
		var operation = undoStack[undoStack.length - 1];
		operation.undo();
		undoStack.pop();
		redoStack.push(operation);
		stateToken = operation.beforeStateToken;
		revision++;
		return true;
	}

	public function redo():Bool {
		ensureNoActiveTransaction();
		if (redoStack.length == 0)
			return false;
		var operation = redoStack[redoStack.length - 1];
		operation.apply();
		redoStack.pop();
		undoStack.push(operation);
		stateToken = operation.afterStateToken;
		revision++;
		return true;
	}

	public function clear():Void {
		ensureNoActiveTransaction();
		undoStack.resize(0);
		redoStack.resize(0);
		revision++;
	}

	@:allow(nativekit.ui.core.EditTransaction)
	function commitTransaction(transaction:EditTransaction):Bool {
		if (activeTransaction != transaction)
			throw "Edit transaction does not belong to this history";
		activeTransaction = null;
		var operations = transaction.operationList();
		if (operations.length == 0)
			return false;
		var composite = new EditOperation(transaction.label,
			function() {
				for (operation in operations)
					operation.apply();
			}, function() {
				var index = operations.length - 1;
				while (index >= 0) {
					operations[index].undo();
					index--;
				}
			}, transaction.coalesceKey, function(next) {
				if (next == null || next.transactionOperations == null)
					return false;
				for (operation in next.transactionOperations)
					operations.push(operation);
				return true;
			}, operations);
		composite.transactionOperations = operations;
		pushApplied(composite, composite.coalesceKey);
		return true;
	}

	@:allow(nativekit.ui.core.EditTransaction)
	function cancelTransaction(transaction:EditTransaction):Bool {
		if (activeTransaction != transaction)
			throw "Edit transaction does not belong to this history";
		activeTransaction = null;
		var operations = transaction.operationList();
		var index = operations.length - 1;
		while (index >= 0) {
			operations[index].undo();
			index--;
		}
		if (operations.length > 0)
			revision++;
		return operations.length > 0;
	}

	function pushApplied(operation:EditOperation, coalesceKey:Null<String>):Void {
		if (coalesceKey != null && undoStack.length > 0) {
			var previous = undoStack[undoStack.length - 1];
			if (previous.coalesceKey == coalesceKey && previous.mergeFrom(operation)) {
				previous.afterStateToken = nextStateToken++;
				stateToken = previous.afterStateToken;
				redoStack.resize(0);
				revision++;
				return;
			}
		}
		operation.beforeStateToken = stateToken;
		operation.afterStateToken = nextStateToken++;
		stateToken = operation.afterStateToken;
		undoStack.push(operation);
		redoStack.resize(0);
		revision++;
	}

	function ensureNoActiveTransaction():Void {
		if (activeTransaction != null)
			throw "Edit history has an open transaction";
	}
}
