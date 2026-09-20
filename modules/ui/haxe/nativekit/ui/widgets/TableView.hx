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
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Virtualized fixed-row table with sticky headers and row selection. */
class TableView implements View {
	public final key:String;
	public final rowCount:Int;
	public final columns:Array<TableColumn>;
	public final rowHeight:Float;
	public final headerHeight:Float;
	public final viewportStyle:LayoutStyle;
	public var controller(default, null):ScrollController;
	public var selectedRow(default, null):Int;
	public var onRowSelected:Null<Int->Void>;
	final cellBuilder:Int->TableColumn->View;
	final rowKeyForIndex:Null<Int->String>;
	final fallbackViewportWidth:Float;
	final fallbackViewportHeight:Float;

	public function new(key:String, rowCount:Int, columns:Array<TableColumn>, rowHeight:Float,
			cellBuilder:Int->TableColumn->View, ?viewportStyle:LayoutStyle,
			?rowKeyForIndex:Int->String, ?controller:ScrollController,
			viewportWidth:Float = 640.0, viewportHeight:Float = 320.0,
			headerHeight:Float = 32.0, selectedRow:Int = -1,
			?onRowSelected:Int->Void) {
		if (key == null || key.length == 0 || rowCount < 0 || columns == null ||
			rowHeight <= 0.0 || headerHeight <= 0.0 || !finite(rowHeight) ||
			!finite(headerHeight) || cellBuilder == null || viewportWidth <= 0.0 ||
			viewportHeight <= 0.0 || !finite(viewportWidth) || !finite(viewportHeight) ||
			selectedRow < -1 || selectedRow >= rowCount)
			throw "TableView requires stable keys, valid dimensions, and a cell builder";
		var columnKeys:Map<String, Bool> = new Map();
		var uniformWidth = columns.length == 0 ? 1.0 : columns[0].width;
		for (column in columns) {
			if (column == null || columnKeys.exists(column.key))
				throw "Table columns require unique metadata keys";
			if (column.width != uniformWidth)
				throw "TableView currently requires fixed-width columns of equal size";
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
		this.onRowSelected = onRowSelected;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var selection:State<Int> = context.state(context.id("selected-row"), selectedRow);
			selectedRow = selection.value;
			var selectRow = function(row:Int, column:Int) {
				if (row == selectedRow)
					return;
				selectedRow = row;
				selection.update(row);
				if (onRowSelected != null)
					onRowSelected(row);
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
				columnWidthAt(0), function(row, column) {
					var columnData = columns[column];
					var child = cellBuilder(row, columnData);
					if (child == null)
						throw 'TableView cell builder returned null for $row,${columnData.key}';
					return new TableSelectionCell("selection", child, selection.value == row);
				}, bodyStyle, function(row, column) {
					var rowKey = rowKeyForIndex == null ? Std.string(row) : rowKeyForIndex(row);
					if (rowKey == null || rowKey.length == 0)
						throw 'TableView row $row has an empty key';
					return '$rowKey:${columns[column].key}';
				}, controller, fallbackViewportWidth, bodyFallbackHeight, selectRow,
				function(row, column) { return selection.value == row; });
			var bodyNode = body.build(context);
			// The table root owns the grid semantics; the nested body remains a scroll region.
			bodyNode.semantics = null;
			var header = new TableHeader("header", columns, headerHeight, totalWidth, controller);
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

	function columnWidthAt(index:Int):Float
		return columns.length == 0 ? 1.0 : columns[index].width;

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

	public function new(key:String, columns:Array<TableColumn>, headerHeight:Float,
			totalWidth:Float, controller:ScrollController) {
		this.key = key;
		this.columns = columns;
		this.headerHeight = headerHeight;
		this.totalWidth = totalWidth;
		this.controller = controller;
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
				headerCells.push(new KeyedView('column:${column.key}',
					new TableHeaderCell("cell", column, index, headerHeight)));
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

	public function new(key:String, column:TableColumn, columnIndex:Int, headerHeight:Float) {
		this.key = key;
		this.column = column;
		this.columnIndex = columnIndex;
		this.headerHeight = headerHeight;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var style = new LayoutStyle();
			style.width = LayoutAxis.fixed(column.width);
			style.height = LayoutAxis.fixed(headerHeight);
			style.background = context.theme.controlUnselected;
			var node = new RenderNode(context.id("table-header-cell"), LayoutVisualKind.Box, style);
			var semantics = new Semantics(AccessibilityRole.ColumnHeader, column.label);
			semantics.columnIndex = columnIndex;
			node.semantics = semantics;
			node.add(context.withScope(new Key("content"), function() {
				return new Text(column.label).build(context);
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
