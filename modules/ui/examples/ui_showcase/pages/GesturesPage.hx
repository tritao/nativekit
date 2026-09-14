package pages;

import Color;
import Insets;
import LayoutAxis;
import LayoutStyle;
import UiExplorer;
import nativekit.ui.core.View;
import nativekit.ui.gestures.DoubleTapRecognizer;
import nativekit.ui.gestures.DragRecognizer;
import nativekit.ui.gestures.LongPressRecognizer;
import nativekit.ui.gestures.TapRecognizer;
import nativekit.ui.widgets.GestureDetector;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Padding;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;

/** Gesture arbitration and Haxe-owned tween/spring motion demonstrations. */
class GesturesPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Gestures & Motion",
			"Haxe recognizers arbitrate taps, holds and drags; frame-ticked tween and spring controllers animate ordinary UI state.");
		var stageStyle = explorer.panelStyle();
		stageStyle.height = LayoutAxis.fixed(174.0);
		var cardStyle = new LayoutStyle();
		cardStyle.width = LayoutAxis.grow();
		cardStyle.height = LayoutAxis.fixed(48.0);
		var gestureCard = explorer.button("Touch, hold, or drag me", "gesture-card-button", function() {
			explorer.state.gestures.message = "Button activation routed through the child view.";
		});
		gestureCard.style.width = LayoutAxis.grow();
		gestureCard.style.height = LayoutAxis.fixed(48.0);
		var detector = new GestureDetector("gesture-playground-detector", gestureCard, [
			new TapRecognizer(function(_) {
				explorer.state.gestures.tapCount++;
				explorer.state.gestures.message = "Tap recognized.";
			}),
			new DoubleTapRecognizer(function(_) {
				explorer.state.gestures.doubleTapCount++;
				explorer.state.gestures.message = "Double tap recognized on the same target.";
			}),
			new LongPressRecognizer(function(_) {
				explorer.state.gestures.longPressCount++;
				explorer.state.gestures.message = "Long press recognized after a stationary hold.";
			}),
			new DragRecognizer(8.0,
				function(_) {
					explorer.state.gestures.dragCount++;
					explorer.state.gestures.dragOriginX = explorer.state.gestures.dragCardX;
					explorer.state.gestures.dragOriginY = explorer.state.gestures.dragCardY;
					explorer.state.gestures.message = "Drag won the gesture arena.";
				},
				function(event) {
					var sidebar = explorer.width < 760.0 ? 176.0 : 212.0;
					var inspectorWidth = explorer.state.inspector.open && explorer.width >= 880.0 ? 270.0 : 0.0;
					var maxX = Math.max(8.0, explorer.width - sidebar - inspectorWidth - 300.0);
					explorer.state.gestures.dragCardX = UiExplorer.clamp(explorer.state.gestures.dragOriginX + event.deltaX,
						8.0, maxX);
					explorer.state.gestures.dragCardY = UiExplorer.clamp(explorer.state.gestures.dragOriginY + event.deltaY,
						8.0, 96.0);
				},
				function(_) { explorer.state.gestures.message = "Drag ended."; })
		], cardStyle);
		items.push(explorer.keyed("gesture-playground", explorer.panel("gesture-playground-card", [
			explorer.keyed("heading", explorer.text("Gesture arena", explorer.paletteText())),
			explorer.keyed("copy", explorer.text("Tap and double-tap the card, hold for a long press, or move past the drag threshold. A recognized drag cancels tap delivery.", explorer.paletteMuted())),
			explorer.keyed("stage", new Stack("gesture-playground-stage", [
				new StackChild("draggable-card", detector, explorer.state.gestures.dragCardX, explorer.state.gestures.dragCardY, 1,
					LayoutAxis.fixed(230.0), LayoutAxis.fixed(48.0))
			], stageStyle)),
			explorer.keyed("gesture-status", explorer.text(explorer.state.gestures.message,
				UiExplorer.color(0.35, 0.85, 0.69))),
			explorer.keyed("gesture-counts", explorer.text('Tap ${explorer.state.gestures.tapCount}  ·  Double tap ${explorer.state.gestures.doubleTapCount}  ·  Long press ${explorer.state.gestures.longPressCount}  ·  Drag ${explorer.state.gestures.dragCount}',
				explorer.paletteMuted()))
		])));

		var motionStyle = explorer.panelStyle();
		motionStyle.height = LayoutAxis.fixed(132.0);
		var tweenX = 8.0 + explorer.state.gestures.tweenValue * 150.0;
		var springX = 8.0 + explorer.state.gestures.springValue * 150.0;
		items.push(explorer.keyed("motion-playground", explorer.panel("motion-playground-card", [
			explorer.keyed("heading", explorer.text("Animated properties", explorer.paletteText())),
			explorer.keyed("copy", explorer.text("These cards move by rebuilding positioned layout from Haxe-owned tween and spring values.", explorer.paletteMuted())),
			explorer.keyed("stage", new Stack("motion-stage", [
				new StackChild("tween-marker", motionMarker("Tween", UiExplorer.color(0.20, 0.52, 0.82)), tweenX, 12.0, 1),
				new StackChild("spring-marker", motionMarker("Spring", UiExplorer.color(0.24, 0.58, 0.48)), springX, 66.0, 1)
			], motionStyle)),
			explorer.keyed("actions", new Row("motion-actions", [
				explorer.keyed("tween", explorer.button("Replay tween", "gesture-replay-tween", function() {
					explorer.tweenController.play(explorer.state.gestures.tweenValue,
						explorer.state.gestures.tweenValue < 0.5 ? 1.0 : 0.0, 0.7);
				})),
				explorer.keyed("spring", explorer.button("Retarget spring", "gesture-retarget-spring", function() {
					explorer.springController.setTarget(explorer.state.gestures.springValue < 0.5 ? 1.0 : 0.18);
				}))
			], explorer.rowStyle(10.0))),
			explorer.keyed("motion-values", explorer.text('Tween ${Std.int(explorer.state.gestures.tweenValue * 100)}%  ·  Spring ${Std.int(explorer.state.gestures.springValue * 100)}%',
				explorer.paletteMuted()))
		])));
	}

	static function motionMarker(label:String, background:Color):View {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(112.0);
		style.height = LayoutAxis.fixed(34.0);
		style.padding = new Insets(9.0, 6.0, 9.0, 6.0);
		style.background = background;
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new Padding("motion-marker-padding", new nativekit.ui.widgets.Text(label, null,
			UiExplorer.color(1.0, 1.0, 1.0)), style.padding, style);
	}
}
