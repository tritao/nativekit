package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.VirtualExtentViewport;

/** Fixed-row virtual grid with optional variable-width columns. */
class VirtualGrid implements View {
	public final key:String;
	public final rowCount:Int;
	public final columnCount:Int;
	public final rowHeight:Float;
	public final columnWidth:Float;
	public final viewportStyle:LayoutStyle;
	public var controller(default, null):ScrollController;
	final cellBuilder:Int->Int->View;
	final keyForCell:Null<Int->Int->String>;
	final onCellActivate:Null<Int->Int->Void>;
	final cellSelected:Null<Int->Int->Bool>;
	final onCellKeyDown:Null<Int->Int->WidgetId->UiEvent->Void>;
	final onCellBuilt:Null<Int->Int->WidgetId->Void>;
	final columnWidths:Null<Array<Float>>;
	final fallbackViewportWidth:Float;
	final fallbackViewportHeight:Float;

	public function new(key:String, rowCount:Int, columnCount:Int, rowHeight:Float,
			columnWidth:Float, cellBuilder:Int->Int->View, ?viewportStyle:LayoutStyle,
			?keyForCell:Int->Int->String, ?controller:ScrollController,
			viewportWidth:Float = 320.0, viewportHeight:Float = 240.0,
			?onCellActivate:Int->Int->Void, ?cellSelected:Int->Int->Bool,
			?onCellKeyDown:Int->Int->WidgetId->UiEvent->Void,
			?onCellBuilt:Int->Int->WidgetId->Void, ?columnWidths:Array<Float>) {
		if (key == null || key.length == 0 || rowCount < 0 || columnCount < 0 ||
			rowHeight <= 0.0 || columnWidth <= 0.0 || !finite(rowHeight) ||
			!finite(columnWidth) || cellBuilder == null || viewportWidth <= 0.0 ||
			viewportHeight <= 0.0 || !finite(viewportWidth) || !finite(viewportHeight))
			throw "VirtualGrid requires stable keys, valid dimensions, and a cell builder";
		this.key = key;
		this.rowCount = rowCount;
		this.columnCount = columnCount;
		this.rowHeight = rowHeight;
		this.columnWidth = columnWidth;
		this.cellBuilder = cellBuilder;
		this.keyForCell = keyForCell;
		this.onCellActivate = onCellActivate;
		this.cellSelected = cellSelected;
		this.onCellKeyDown = onCellKeyDown;
		this.onCellBuilt = onCellBuilt;
		if (columnWidths != null && columnWidths.length != columnCount)
			throw "VirtualGrid column extents must match the column count";
		if (columnWidths != null)
			for (width in columnWidths)
				if (width <= 0.0 || !finite(width))
					throw "VirtualGrid column extents must be finite and positive";
		this.columnWidths = columnWidths == null ? null : columnWidths.copy();
		this.controller = controller == null ? new ScrollController() : controller;
		this.fallbackViewportWidth = viewportWidth;
		this.fallbackViewportHeight = viewportHeight;
		this.viewportStyle = viewportStyle == null ? defaultViewportStyle(viewportWidth,
			viewportHeight) : viewportStyle.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var stateId = context.id("scroll-state");
			var stored:State<ScrollController> = context.state(stateId, controller);
			controller = stored.value;
			var viewportWidth = controller.viewportWidth > 0.0 ? controller.viewportWidth :
				(viewportStyle.width.sizing == LayoutSizing.Fixed ? viewportStyle.width.value :
				fallbackViewportWidth);
			var viewportHeight = controller.viewportHeight > 0.0 ? controller.viewportHeight :
				(viewportStyle.height.sizing == LayoutSizing.Fixed ? viewportStyle.height.value :
				fallbackViewportHeight);
			var rowWindow = new VirtualViewport(rowCount, rowHeight, viewportHeight,
				controller.offsetY);
			var firstColumn = 0;
			var lastColumn = 0;
			var beforeWidth = 0.0;
			var afterWidth = 0.0;
			var contentWidth = columnCount * columnWidth;
			if (columnWidths == null) {
				var fixedColumnWindow = new VirtualViewport(columnCount, columnWidth, viewportWidth,
					controller.offsetX);
				firstColumn = fixedColumnWindow.first;
				lastColumn = fixedColumnWindow.last;
				beforeWidth = firstColumn * columnWidth;
				afterWidth = (columnCount - lastColumn) * columnWidth;
			} else {
				var extentColumnWindow = new VirtualExtentViewport(columnWidths, viewportWidth,
					controller.offsetX);
				firstColumn = extentColumnWindow.first;
				lastColumn = extentColumnWindow.last;
				beforeWidth = extentColumnWindow.startOffset(firstColumn);
				afterWidth = extentColumnWindow.totalExtent - extentColumnWindow.startOffset(lastColumn);
				contentWidth = extentColumnWindow.totalExtent;
			}
			var rowViews:Array<KeyedView> = [];
			if (rowWindow.count > 0 && lastColumn > firstColumn) {
				var beforeHeight = rowWindow.first * rowHeight;
				rowViews.push(new KeyedView("before", new Spacer("before-spacer",
					LayoutAxis.grow(), LayoutAxis.fixed(beforeHeight))));
				for (row in rowWindow.first...rowWindow.last) {
					var gridRow = new VirtualGridRow("row", row, rowCount, columnCount,
						rowHeight, columnWidth, columnWidths, firstColumn, lastColumn,
						beforeWidth, afterWidth, contentWidth, cellBuilder, keyForCell,
						onCellActivate, cellSelected, onCellKeyDown, onCellBuilt);
					rowViews.push(new KeyedView('row:$row', gridRow));
				}
				var afterHeight = (rowCount - rowWindow.last) * rowHeight;
				rowViews.push(new KeyedView("after", new Spacer("after-spacer",
					LayoutAxis.grow(), LayoutAxis.fixed(afterHeight))));
			}

			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.fixed(contentWidth);
			contentStyle.height = LayoutAxis.fixed(rowCount * rowHeight);
			var content = new Column("virtual-grid-content", rowViews, contentStyle);
			var scroll = new ScrollView("viewport", content, viewportStyle,
				ScrollAxis.Both, controller);
			var root = new RenderNode(context.id("grid"), LayoutVisualKind.Box);
			root.layout.style.width = viewportStyle.width;
			root.layout.style.height = viewportStyle.height;
			var semantics = new Semantics(AccessibilityRole.Grid);
			semantics.rowCount = rowCount;
			semantics.columnCount = columnCount;
			semantics.setSize = rowCount * columnCount;
			root.semantics = semantics;
			var viewport = context.withScope(new Key("scroll-view"), function() return
				scroll.build(context));
			root.add(viewport);
			return root;
		});
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

private class VirtualGridRow implements View {
	final key:String;
	final rowIndex:Int;
	final rowCount:Int;
	final columnCount:Int;
	final rowHeight:Float;
	final columnWidth:Float;
	final columnWidths:Null<Array<Float>>;
	final firstColumn:Int;
	final lastColumn:Int;
	final beforeWidth:Float;
	final afterWidth:Float;
	final contentWidth:Float;
	final cellBuilder:Int->Int->View;
	final keyForCell:Null<Int->Int->String>;
	final onCellActivate:Null<Int->Int->Void>;
	final cellSelected:Null<Int->Int->Bool>;
	final onCellKeyDown:Null<Int->Int->WidgetId->UiEvent->Void>;
	final onCellBuilt:Null<Int->Int->WidgetId->Void>;

	public function new(key:String, rowIndex:Int, rowCount:Int, columnCount:Int,
			rowHeight:Float, columnWidth:Float, columnWidths:Null<Array<Float>>,
			firstColumn:Int, lastColumn:Int, beforeWidth:Float, afterWidth:Float,
			contentWidth:Float,
			cellBuilder:Int->Int->View, keyForCell:Null<Int->Int->String>,
			onCellActivate:Null<Int->Int->Void>, cellSelected:Null<Int->Int->Bool>,
			onCellKeyDown:Null<Int->Int->WidgetId->UiEvent->Void>,
			onCellBuilt:Null<Int->Int->WidgetId->Void>) {
		this.key = key;
		this.rowIndex = rowIndex;
		this.rowCount = rowCount;
		this.columnCount = columnCount;
		this.rowHeight = rowHeight;
		this.columnWidth = columnWidth;
		this.columnWidths = columnWidths;
		this.firstColumn = firstColumn;
		this.lastColumn = lastColumn;
		this.beforeWidth = beforeWidth;
		this.afterWidth = afterWidth;
		this.contentWidth = contentWidth;
		this.cellBuilder = cellBuilder;
		this.keyForCell = keyForCell;
		this.onCellActivate = onCellActivate;
		this.cellSelected = cellSelected;
		this.onCellKeyDown = onCellKeyDown;
		this.onCellBuilt = onCellBuilt;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var children:Array<KeyedView> = [];
			children.push(new KeyedView("before", new Spacer("before-spacer",
				LayoutAxis.fixed(beforeWidth), LayoutAxis.grow())));
			for (column in firstColumn...lastColumn) {
				var cellKey = keyForCell == null ? '$rowIndex:$column' :
					keyForCell(rowIndex, column);
				if (cellKey == null || cellKey.length == 0)
					throw 'VirtualGrid cell $rowIndex,$column has an empty key';
				var cell = cellBuilder(rowIndex, column);
				if (cell == null)
					throw 'VirtualGrid cell builder returned null for $rowIndex,$column';
				var cellWidth = columnWidths == null ? columnWidth : columnWidths[column];
				children.push(new KeyedView('cell:$cellKey', new VirtualGridCell("cell", cell,
					rowCount, columnCount, rowHeight, cellWidth, rowIndex, column,
					onCellActivate, cellSelected, onCellKeyDown, onCellBuilt)));
			}
			children.push(new KeyedView("after", new Spacer("after-spacer",
				LayoutAxis.fixed(afterWidth), LayoutAxis.grow())));
			var style = new LayoutStyle();
			style.width = LayoutAxis.fixed(contentWidth);
			style.height = LayoutAxis.fixed(rowHeight);
			var row = new Row("row", children, style).build(context);
			var semantics = new Semantics(AccessibilityRole.Row);
			semantics.rowCount = rowCount;
			semantics.columnCount = columnCount;
			semantics.rowIndex = rowIndex;
			row.semantics = semantics;
			return row;
		});
	}
}

private class VirtualGridCell implements View {
	final key:String;
	final child:View;
	final rowCount:Int;
	final columnCount:Int;
	final rowIndex:Int;
	final columnIndex:Int;
	final rowHeight:Float;
	final columnWidth:Float;
	final onCellActivate:Null<Int->Int->Void>;
	final cellSelected:Null<Int->Int->Bool>;
	final onCellKeyDown:Null<Int->Int->WidgetId->UiEvent->Void>;
	final onCellBuilt:Null<Int->Int->WidgetId->Void>;

	public function new(key:String, child:View, rowCount:Int, columnCount:Int,
			rowHeight:Float, columnWidth:Float, rowIndex:Int, columnIndex:Int,
			onCellActivate:Null<Int->Int->Void>, cellSelected:Null<Int->Int->Bool>,
			onCellKeyDown:Null<Int->Int->WidgetId->UiEvent->Void>,
			onCellBuilt:Null<Int->Int->WidgetId->Void>) {
		this.key = key;
		this.child = child;
		this.rowCount = rowCount;
		this.columnCount = columnCount;
		this.rowIndex = rowIndex;
		this.columnIndex = columnIndex;
		this.rowHeight = rowHeight;
		this.columnWidth = columnWidth;
		this.onCellActivate = onCellActivate;
		this.cellSelected = cellSelected;
		this.onCellKeyDown = onCellKeyDown;
		this.onCellBuilt = onCellBuilt;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var style = new LayoutStyle();
			style.width = LayoutAxis.fixed(columnWidth);
			style.height = LayoutAxis.fixed(rowHeight);
			var node = new RenderNode(context.id("grid-cell"), LayoutVisualKind.Box, style);
			node.focusable = onCellActivate != null || onCellKeyDown != null;
			var semantics = new Semantics(AccessibilityRole.Cell);
			semantics.rowCount = rowCount;
			semantics.columnCount = columnCount;
			semantics.rowIndex = rowIndex;
			semantics.columnIndex = columnIndex;
			if (cellSelected != null && cellSelected(rowIndex, columnIndex))
				semantics.states |= AccessibilityState.Selected;
			if (onCellActivate != null) {
				var callback:Int->Int->Void = cast onCellActivate;
				semantics.actions = AccessibilityAction.Activate;
				var activate = function(event:UiEvent) {
					callback(rowIndex, columnIndex);
				};
				node.on(UiEventKind.Click, activate);
				node.on(UiEventKind.Activate, activate);
			}
			if (onCellKeyDown != null) {
				var keyCallback:Int->Int->WidgetId->UiEvent->Void = cast onCellKeyDown;
				node.on(UiEventKind.KeyDown, function(event:UiEvent) {
					keyCallback(rowIndex, columnIndex, node.id, event);
				});
			}
			if (onCellBuilt != null) {
				var builtCallback:Int->Int->WidgetId->Void = cast onCellBuilt;
				builtCallback(rowIndex, columnIndex, node.id);
			}
			node.semantics = semantics;
			node.add(context.withScope(new Key("content"), function() return child.build(context)));
			return node;
		});
	}
}
