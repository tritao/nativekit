package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Transform2D;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Virtualized variable-width table with sticky headers, selection, and sorting. */
class TableView implements View {
	static inline var minimumColumnWidth:Float = 24.0;

	public final key:String;
	public final rowCount:Int;
	public final columns:Array<TableColumn>;
	public final rowHeight:Float;
	public final headerHeight:Float;
	public final viewportStyle:LayoutStyle;
	public var controller(default, null):ScrollController;
	public var selectedRow(default, null):Int;
	public var selectedColumn(default, null):Int;
	public var sortColumn(default, null):Int;
	public var sortAscending(default, null):Bool;
	public var onRowSelected:Null<Int->Void>;
	public var onCellSelected:Null<Int->Int->Void>;
	public var onSort:Null<Int->Bool->Void>;
	public var onColumnResized:Null<Int->Float->Void>;
	final cellBuilder:Int->TableColumn->View;
	final rowKeyForIndex:Null<Int->String>;
	final fallbackViewportWidth:Float;
	final fallbackViewportHeight:Float;
	var columnRevision:Int;
	var columnRevisionState:Null<State<Int>>;
	var cellIds:Map<String, WidgetId>;

	public function new(key:String, rowCount:Int, columns:Array<TableColumn>, rowHeight:Float,
			cellBuilder:Int->TableColumn->View, ?viewportStyle:LayoutStyle,
			?rowKeyForIndex:Int->String, ?controller:ScrollController,
			viewportWidth:Float = 640.0, viewportHeight:Float = 320.0,
			headerHeight:Float = 32.0, selectedRow:Int = -1, ?onRowSelected:Int->Void,
			selectedColumn:Int = 0, ?onCellSelected:Int->Int->Void,
			?onSort:Int->Bool->Void, ?onColumnResized:Int->Float->Void) {
		if (key == null || key.length == 0 || rowCount < 0 || columns == null ||
			rowHeight <= 0.0 || headerHeight <= 0.0 || !finite(rowHeight) ||
			!finite(headerHeight) || cellBuilder == null || viewportWidth <= 0.0 ||
			viewportHeight <= 0.0 || !finite(viewportWidth) || !finite(viewportHeight) ||
			selectedRow < -1 || selectedRow >= rowCount || selectedColumn < -1 ||
			(columns.length == 0 && selectedColumn >= 0) ||
			(columns.length > 0 && selectedColumn >= columns.length))
			throw "TableView requires stable keys, valid dimensions, and a cell builder";
		var columnKeys:Map<String, Bool> = new Map();
		for (column in columns) {
			if (column == null || columnKeys.exists(column.key))
				throw "Table columns require unique metadata keys";
			columnKeys.set(column.key, true);
		}
		this.key = key;
		this.rowCount = rowCount;
		this.columns = columns.copy();
		this.rowHeight = rowHeight;
		this.headerHeight = headerHeight;
		this.viewportStyle = viewportStyle == null ? defaultViewportStyle(viewportWidth,
			viewportHeight) : viewportStyle.copy();
		this.cellBuilder = cellBuilder;
		this.rowKeyForIndex = rowKeyForIndex;
		this.controller = controller == null ? new ScrollController() : controller;
		this.fallbackViewportWidth = viewportWidth;
		this.fallbackViewportHeight = viewportHeight;
		this.selectedRow = selectedRow;
		this.selectedColumn = selectedColumn;
		this.sortColumn = -1;
		this.sortAscending = true;
		this.onRowSelected = onRowSelected;
		this.onCellSelected = onCellSelected;
		this.onSort = onSort;
		this.onColumnResized = onColumnResized;
		columnRevision = 0;
		columnRevisionState = null;
		cellIds = new Map();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			columnRevisionState = context.state(context.id("column-revision"), columnRevision);
			columnRevision = columnRevisionState.value;
			var selectionRow:State<Int> = context.state(context.id("selected-row"), selectedRow);
			var selectionColumn:State<Int> = context.state(context.id("selected-column"), selectedColumn);
			selectedRow = selectionRow.value;
			selectedColumn = selectionColumn.value;
			var selectCell = function(row:Int, column:Int) {
				if (row < 0 || row >= rowCount || column < 0 || column >= columns.length)
					return;
				var rowChanged = row != selectedRow;
				var columnChanged = column != selectedColumn;
				if (!rowChanged && !columnChanged)
					return;
				selectedRow = row;
				selectedColumn = column;
				selectionRow.update(row);
				selectionColumn.update(column);
				if (rowChanged && onRowSelected != null)
					onRowSelected(row);
				if (onCellSelected != null)
					onCellSelected(row, column);
			};
			cellIds = new Map();
			var handleCellKeyDown = function(row:Int, column:Int, id:WidgetId, event:UiEvent) {
				var nextRow = row;
				var nextColumn = column;
				switch (event.key) {
					case UiKey.Left: nextColumn--;
					case UiKey.Right: nextColumn++;
					case UiKey.Up: nextRow--;
					case UiKey.Down: nextRow++;
					case UiKey.Home: nextColumn = 0;
					case UiKey.End: nextColumn = columns.length - 1;
					case UiKey.PageUp: nextRow -= visibleRowCount();
					case UiKey.PageDown: nextRow += visibleRowCount();
					default: return;
				}
				nextRow = Std.int(Math.max(0, Math.min(rowCount - 1, nextRow)));
				nextColumn = Std.int(Math.max(0, Math.min(columns.length - 1, nextColumn)));
				if (nextRow == row && nextColumn == column)
					return;
				event.preventDefault();
				selectCell(nextRow, nextColumn);
				var target = cellIds.get(cellKey(nextRow, nextColumn));
				if (target != null)
					context.requestFocus(target);
				else
					controller.jumpTo(columnOffset(nextColumn), nextRow * rowHeight);
			};
			var totalWidth = columnWidthTotal();
			var tableHeight = viewportStyle.height.sizing == LayoutSizing.Fixed
				? viewportStyle.height.value : fallbackViewportHeight;
			var bodyFallbackHeight = Math.max(1.0, tableHeight - headerHeight);
			var bodyStyle = viewportStyle.copy();
			if (viewportStyle.height.sizing == LayoutSizing.Fixed)
				bodyStyle.height = LayoutAxis.fixed(bodyFallbackHeight);
			else
				bodyStyle.height = LayoutAxis.grow();
			var body = new VirtualGrid('${key}-body', rowCount, columns.length, rowHeight,
				columnWidthAt(0), function(row:Int, column:Int) {
					var columnData = columns[column];
					var child = cellBuilder(row, columnData);
					if (child == null)
						throw 'TableView cell builder returned null for $row,${columnData.key}';
					return new TableSelectionCell("selection", child,
						selectionRow.value == row && selectionColumn.value == column);
				}, bodyStyle, function(row:Int, column:Int) {
					var rowKey = rowKeyForIndex == null ? Std.string(row) : rowKeyForIndex(row);
					if (rowKey == null || rowKey.length == 0)
						throw 'TableView row $row has an empty key';
					return '$rowKey:${columns[column].key}';
				}, controller, fallbackViewportWidth, bodyFallbackHeight, selectCell,
				function(row:Int, column:Int) {
					return selectionRow.value == row && selectionColumn.value == column;
				}, handleCellKeyDown, function(row:Int, column:Int, id:WidgetId) {
					cellIds.set(cellKey(row, column), id);
				}, columnWidths());
			var bodyNode = body.build(context);
			// The table root owns the grid semantics; the nested body remains a scroll region.
			bodyNode.semantics = null;
			var header = new TableHeader("header", columns, headerHeight, totalWidth, controller,
				function(index:Int, delta:Float) { resizeColumn(index, columns[index].width + delta); },
				function(index:Int) { toggleSort(index); }, sortColumn, sortAscending);
			var children:Array<KeyedView> = [
				new KeyedView("header", header),
				new KeyedView("body", body)
			];
			var root = new Column("table-content", children, viewportStyle).build(context);
			var semantics = new Semantics(AccessibilityRole.Grid);
			semantics.rowCount = rowCount;
			semantics.columnCount = columns.length;
			semantics.setSize = rowCount * columns.length;
			root.semantics = semantics;
			return root;
		});
	}

	/** Resizes a column, clamping interactive changes to a usable minimum. */
	public function resizeColumn(index:Int, width:Float):Bool {
		if (index < 0 || index >= columns.length)
			throw "Table column index is out of range";
		if (!finite(width))
			throw "Table column width must be finite";
		var next = Math.max(minimumColumnWidth, width);
		if (!columns[index].resize(next))
			return false;
		columnRevision++;
		if (columnRevisionState != null)
			columnRevisionState.update(columnRevision);
		if (onColumnResized != null)
			onColumnResized(index, next);
		return true;
	}

	function toggleSort(index:Int):Void {
		if (sortColumn == index)
			sortAscending = !sortAscending;
		else {
			sortColumn = index;
			sortAscending = true;
		}
		if (onSort != null)
			onSort(sortColumn, sortAscending);
	}

	function visibleRowCount():Int
		return Std.int(Math.max(1.0, Math.ceil((fallbackViewportHeight - headerHeight) / rowHeight)));

	function cellKey(row:Int, column:Int):String
		return '$row:$column';

	function columnWidthAt(index:Int):Float
		return columns.length == 0 ? 1.0 : columns[index].width;

	function columnWidths():Array<Float> {
		var result:Array<Float> = [];
		for (column in columns)
			result.push(column.width);
		return result;
	}

	function columnOffset(index:Int):Float {
		var result = 0.0;
		for (columnIndex in 0...index)
			result += columns[columnIndex].width;
		return result;
	}

	function columnWidthTotal():Float {
		var result = 0.0;
		for (column in columns)
			result += column.width;
		return result;
	}

	static function defaultViewportStyle(width:Float, height:Float):LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(width);
		result.height = LayoutAxis.fixed(height);
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}

private class TableHeader implements View {
	final key:String;
	final columns:Array<TableColumn>;
	final headerHeight:Float;
	final totalWidth:Float;
	final controller:ScrollController;
	final onResize:Int->Float->Void;
	final onSort:Int->Void;
	final sortColumn:Int;
	final sortAscending:Bool;

	public function new(key:String, columns:Array<TableColumn>, headerHeight:Float,
			totalWidth:Float, controller:ScrollController, onResize:Int->Float->Void,
			onSort:Int->Void, sortColumn:Int, sortAscending:Bool) {
		this.key = key;
		this.columns = columns;
		this.headerHeight = headerHeight;
		this.totalWidth = totalWidth;
		this.controller = controller;
		this.onResize = onResize;
		this.onSort = onSort;
		this.sortColumn = sortColumn;
		this.sortAscending = sortAscending;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var viewportStyle = new LayoutStyle();
			viewportStyle.width = LayoutAxis.grow();
			viewportStyle.height = LayoutAxis.fixed(headerHeight);
			viewportStyle.clipHorizontal = true;
			viewportStyle.clipVertical = true;
			var viewport = new RenderNode(context.id("header-viewport"),
				LayoutVisualKind.Box, viewportStyle);
			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.fixed(totalWidth);
			contentStyle.height = LayoutAxis.fixed(headerHeight);
			contentStyle.transform = Transform2D.identity().translated(-controller.offsetX, 0.0);
			var headerCells:Array<KeyedView> = [];
			for (index in 0...columns.length) {
				var column = columns[index];
				var columnIndex = index;
				headerCells.push(new KeyedView('column:${column.key}',
					new TableHeaderCell("cell", column, columnIndex, headerHeight,
						sortColumn == columnIndex ? (sortAscending ? " ↑" : " ↓") : "",
						function(delta:Float) { onResize(columnIndex, delta); },
						function() { onSort(columnIndex); })));
			}
			var content = new Row("header-content", headerCells, contentStyle).build(context);
			var rowSemantics = new Semantics(AccessibilityRole.Row);
			rowSemantics.columnCount = columns.length;
			content.semantics = rowSemantics;
			viewport.add(content);
			return viewport;
		});
	}
}

private class TableHeaderCell implements View {
	final key:String;
	final column:TableColumn;
	final columnIndex:Int;
	final headerHeight:Float;
	final sortSuffix:String;
	final onResize:Float->Void;
	final onSort:Void->Void;

	public function new(key:String, column:TableColumn, columnIndex:Int, headerHeight:Float,
			sortSuffix:String, onResize:Float->Void, onSort:Void->Void) {
		this.key = key;
		this.column = column;
		this.columnIndex = columnIndex;
		this.headerHeight = headerHeight;
		this.sortSuffix = sortSuffix;
		this.onResize = onResize;
		this.onSort = onSort;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var style = new LayoutStyle();
			style.width = LayoutAxis.fixed(column.width);
			style.height = LayoutAxis.fixed(headerHeight);
			style.background = context.theme.controlUnselected;
			var node = new RenderNode(context.id("table-header-cell"), LayoutVisualKind.Box, style);
			node.focusable = true;
			var semantics = new Semantics(AccessibilityRole.ColumnHeader, column.label);
			semantics.columnIndex = columnIndex;
			semantics.actions = AccessibilityAction.Activate;
			node.semantics = semantics;
			var resizing = false;
			var didResize = false;
			var lastX = 0.0;
			node.on(UiEventKind.PointerDown, function(event:UiEvent) {
				didResize = false;
				if (event.button != 0 || event.localX < column.width - 8.0)
					return;
				resizing = true;
				didResize = false;
				lastX = event.globalX;
				event.capturePointer();
				event.preventDefault();
			});
			node.on(UiEventKind.PointerMove, function(event:UiEvent) {
				if (!resizing)
					return;
				var delta = event.globalX - lastX;
				lastX = event.globalX;
				if (delta != 0.0) {
					onResize(delta);
					didResize = true;
				}
				event.preventDefault();
			});
			var finishResize = function(event:UiEvent) {
				if (!resizing)
					return;
				resizing = false;
				event.releasePointer();
				event.preventDefault();
			};
			node.on(UiEventKind.PointerUp, finishResize);
			node.on(UiEventKind.PointerCancel, finishResize);
			node.on(UiEventKind.Click, function(event:UiEvent) {
				if (didResize) {
					didResize = false;
					return;
				}
				onSort();
			});
			node.on(UiEventKind.Activate, function(event:UiEvent) { onSort(); });
			node.add(context.withScope(new Key("content"), function() {
				return new Text(column.label + sortSuffix).build(context);
			}));
			return node;
		});
	}
}

private class TableSelectionCell implements View {
	final key:String;
	final child:View;
	final selected:Bool;

	public function new(key:String, child:View, selected:Bool) {
		this.key = key;
		this.child = child;
		this.selected = selected;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var style = new LayoutStyle();
			style.width = LayoutAxis.grow();
			style.height = LayoutAxis.grow();
			style.background = selected ? context.theme.tokens.selectionHighlight :
				Color.rgba(0.0, 0.0, 0.0, 0.0);
			var node = new RenderNode(context.id("table-selection"), LayoutVisualKind.Box, style);
			node.add(context.withScope(new Key("content"), function() return child.build(context)));
			return node;
		});
	}
}
