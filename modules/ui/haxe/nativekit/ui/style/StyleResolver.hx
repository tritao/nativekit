package nativekit.ui.style;

import Color;
import LayoutStyle;
import Transform2D;
import nativekit.ui.animation.AnimationScheduler;

/** Deterministic framework/inheritance/theme/application/state/local cascade. */
class StyleResolver {
	final frameworkSource:StyleSource;
	final scheduler:Null<AnimationScheduler>;
	final animatedValues:Map<String, Dynamic>;
	final targets:Map<String, Dynamic>;
	final activeAnimations:Map<String, StyleTransitionAnimation>;

	public function new(?scheduler:AnimationScheduler) {
		frameworkSource = new StyleSource("framework", "default", -1, "framework");
		this.scheduler = scheduler;
		animatedValues = new Map();
		targets = new Map();
		activeAnimations = new Map();
	}

	public function resolve(target:StyleTarget, ?parent:ComputedStyle,
			?theme:StyleSheet, ?application:StyleSheet, ?local:LayoutStyle):ComputedStyle {
		if (target == null)
			throw "Style resolution requires a target";
		var result = new ComputedStyle();
		for (property in StyleProperty.all())
			result.set(property, property.defaultValue, frameworkSource);

		if (parent != null)
			for (property in StyleProperty.all())
				if (property.inherited && parent.has(property))
					result.set(property, parent.get(property), parent.source(property));

		applySheet(result, target, theme, false, "theme");
		applySheet(result, target, application, false, "application");
		applySheet(result, target, theme, true, "theme-state");
		applySheet(result, target, application, true, "application-state");
		applyLocal(result, local);
		applyTransitions(result, target, theme, application);
		return result;
	}

	function applySheet(result:ComputedStyle, target:StyleTarget, sheet:Null<StyleSheet>,
			stateRules:Bool, layer:String):Void {
		if (sheet == null || sheet.isEmpty())
			return;
		var matches:Array<StyleRule> = [];
		for (rule in sheet.rules)
			if (rule.selector.hasState() == stateRules && rule.matches(target))
				matches.push(rule);
		matches.sort(function(left, right) {
			var specificity = left.selector.specificity() - right.selector.specificity();
			return specificity == 0 ? left.order - right.order : specificity;
		});
		for (rule in matches) {
			var source = new StyleSource(sheet.name, rule.selector.describe(), rule.order, layer);
			result.recordMatch(source);
			for (declaration in rule.declarations)
				result.set(declaration.property, declaration.value, source);
		}
	}

	function applyLocal(result:ComputedStyle, local:Null<LayoutStyle>):Void {
		if (local == null)
			return;
		var source = new StyleSource("local", "widget", -1, "local");
		var defaults = new LayoutStyle();
		if (local.width.sizing != defaults.width.sizing || local.width.value != defaults.width.value)
			result.set(StyleProperty.Width, local.width, source);
		if (local.height.sizing != defaults.height.sizing || local.height.value != defaults.height.value)
			result.set(StyleProperty.Height, local.height, source);
		if (local.direction != defaults.direction)
			result.set(StyleProperty.Direction, local.direction, source);
		if (local.childAlignX != defaults.childAlignX)
			result.set(StyleProperty.ChildAlignX, local.childAlignX, source);
		if (local.childAlignY != defaults.childAlignY)
			result.set(StyleProperty.ChildAlignY, local.childAlignY, source);
		if (local.positioning != defaults.positioning)
			result.set(StyleProperty.Positioning, local.positioning, source);
		if (local.positionX != defaults.positionX)
			result.set(StyleProperty.PositionX, local.positionX, source);
		if (local.positionY != defaults.positionY)
			result.set(StyleProperty.PositionY, local.positionY, source);
		if (local.zIndex != defaults.zIndex)
			result.set(StyleProperty.ZIndex, local.zIndex, source);
		if (local.clipToParent != defaults.clipToParent)
			result.set(StyleProperty.ClipToParent, local.clipToParent, source);
		if (local.padding.left != defaults.padding.left || local.padding.top != defaults.padding.top ||
			local.padding.right != defaults.padding.right || local.padding.bottom != defaults.padding.bottom)
			result.set(StyleProperty.Padding, local.padding, source);
		if (local.childGap != defaults.childGap)
			result.set(StyleProperty.ChildGap, local.childGap, source);
		if (!sameColor(local.background, defaults.background))
			result.set(StyleProperty.Background, local.background, source);
		if (local.radiusTopLeft != defaults.radiusTopLeft)
			result.set(StyleProperty.RadiusTopLeft, local.radiusTopLeft, source);
		if (local.radiusTopRight != defaults.radiusTopRight)
			result.set(StyleProperty.RadiusTopRight, local.radiusTopRight, source);
		if (local.radiusBottomRight != defaults.radiusBottomRight)
			result.set(StyleProperty.RadiusBottomRight, local.radiusBottomRight, source);
		if (local.radiusBottomLeft != defaults.radiusBottomLeft)
			result.set(StyleProperty.RadiusBottomLeft, local.radiusBottomLeft, source);
		if (local.clipHorizontal != defaults.clipHorizontal)
			result.set(StyleProperty.ClipHorizontal, local.clipHorizontal, source);
		if (local.clipVertical != defaults.clipVertical)
			result.set(StyleProperty.ClipVertical, local.clipVertical, source);
		if (local.visible != defaults.visible)
			result.set(StyleProperty.Visible, local.visible, source);
		if (!sameTransform(local.transform, defaults.transform))
			result.set(StyleProperty.Transform, local.transform, source);
	}

	static function sameColor(left:Color, right:Color):Bool
		return left == right || (left != null && right != null && left.red == right.red &&
			left.green == right.green && left.blue == right.blue && left.alpha == right.alpha);

	static function sameTransform(left:Transform2D, right:Transform2D):Bool
		return left == right || (left != null && right != null && left.a == right.a &&
			left.b == right.b && left.c == right.c && left.d == right.d &&
			left.tx == right.tx && left.ty == right.ty);

	function applyTransitions(result:ComputedStyle, target:StyleTarget,
			theme:Null<StyleSheet>, application:Null<StyleSheet>):Void {
		if (scheduler == null)
			return;
		for (property in StyleProperty.all()) {
			var transition = transitionFor(property, theme, application);
			if (transition == null || property.interpolate == null || !result.has(property))
				continue;
			var key = animationKey(target, property);
			var next = result.get(property);
			var previousTarget = targets.get(key);
			if (!targets.exists(key)) {
				targets.set(key, next);
				animatedValues.set(key, next);
			} else if (!sameValue(property, previousTarget, next)) {
				targets.set(key, next);
				var current = animatedValues.get(key);
				stop(key);
				var animation = new StyleTransitionAnimation(current, next, transition.duration,
					transition.easing, property.name,
					function(value) { animatedValues.set(key, value); },
					function() { animatedValues.set(key, next); activeAnimations.remove(key); });
				activeAnimations.set(key, animation);
				scheduler.track(animation);
			}
			if (animatedValues.exists(key))
				result.set(property, animatedValues.get(key), result.source(property));
		}
	}

	function transitionFor(property:StyleProperty<Dynamic>, theme:Null<StyleSheet>,
			application:Null<StyleSheet>):Null<StyleTransition> {
		var result = application == null ? null : application.transitionFor(property);
		return result == null && theme != null ? theme.transitionFor(property) : result;
	}

	function stop(key:String):Void {
		var active = activeAnimations.get(key);
		if (active != null && scheduler != null)
			scheduler.remove(active);
		activeAnimations.remove(key);
	}

	static function animationKey(target:StyleTarget, property:StyleProperty<Dynamic>):String {
		var identity = target.id != null ? target.id : target.key != null ? target.key : target.widgetType;
		return identity + "|" + property.name;
	}

	static function sameValue(property:StyleProperty<Dynamic>, left:Dynamic, right:Dynamic):Bool {
		if (left == right)
			return true;
		return switch property.name {
			case "background" | "borderColor" | "outlineColor" | "shadowColor":
				var leftColor:Color = cast left;
				var rightColor:Color = cast right;
				sameColor(leftColor, rightColor);
			case "width" | "height" | "radiusTopLeft" | "radiusTopRight" |
				"radiusBottomRight" | "radiusBottomLeft" | "opacity" | "shadowOffsetX" |
				"shadowOffsetY" | "shadowBlur" | "fontSize" | "letterSpacing":
				var leftFloat:Float = cast left;
				var rightFloat:Float = cast right;
				leftFloat == rightFloat;
			default:
				false;
		}
	}
}
