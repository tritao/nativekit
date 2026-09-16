package nativekit.ui.style;

import Color;
import Insets;
import LayoutAxis;
import LayoutStyle;
import Transform2D;
import nativekit.ui.animation.AnimationScheduler;

/** Deterministic framework/inheritance/theme/application/state/local cascade. */
class StyleResolver {
	static inline var MaxCacheEntries:Int = 8192;

	final frameworkSource:StyleSource;
	final scheduler:Null<AnimationScheduler>;
	final animatedValues:Map<String, Dynamic>;
	final targets:Map<String, Dynamic>;
	final activeAnimations:Map<String, StyleTransitionAnimation>;
	final cache:Map<String, ComputedStyle>;
	final localFingerprints:Map<Int, StyleResolverLocalFingerprint>;
	var cacheEntryCount:Int;
	var cacheHitCount:Int;
	var cacheMissCount:Int;
	var resolutionCount:Int;
	var localFingerprintCount:Int;
	var nextLocalFingerprint:Int;

	/** Number of cacheable resolutions served from the computed-style cache. */
	public var cacheHits(get, never):Int;
	/** Number of cacheable resolutions that required a fresh cascade. */
	public var cacheMisses(get, never):Int;
	/** Total number of style resolution calls made by this resolver. */
	public var resolutions(get, never):Int;
	/** Number of entries currently held by this resolver's bounded cache. */
	public var cachedStyleCount(get, never):Int;

	public function new(?scheduler:AnimationScheduler) {
		frameworkSource = new StyleSource("framework", "default", -1, "framework");
		this.scheduler = scheduler;
		animatedValues = new Map();
		targets = new Map();
		activeAnimations = new Map();
		cache = new Map();
		localFingerprints = new Map();
		cacheEntryCount = 0;
		cacheHitCount = 0;
		cacheMissCount = 0;
		resolutionCount = 0;
		localFingerprintCount = 0;
		nextLocalFingerprint = 0;
	}

	function get_cacheHits():Int
		return cacheHitCount;

	function get_cacheMisses():Int
		return cacheMissCount;

	function get_resolutions():Int
		return resolutionCount;

	function get_cachedStyleCount():Int
		return cacheEntryCount;

	/** Drops resolved styles while retaining animation state and counters. */
	public function clearCache():Void {
		cache.clear();
		localFingerprints.clear();
		cacheEntryCount = 0;
		localFingerprintCount = 0;
	}

	public function resolve(target:StyleTarget, ?parent:ComputedStyle,
			?theme:StyleSheet, ?application:StyleSheet, ?local:LayoutStyle,
			?environment:StyleEnvironment):ComputedStyle {
		if (target == null)
			throw "Style resolution requires a target";
		resolutionCount++;

		// Transition values depend on scheduler time and must be resolved live. A
		// sheet containing transitions conservatively disables caching for the
		// whole target, while ordinary styles remain pure and cacheable.
		var cacheable = !hasTransitions(theme, application);
		var cacheKey = cacheable ? makeCacheKey(target, parent, theme, application, local, environment) : null;
		if (cacheable) {
			var cached = cache.get(cacheKey);
			if (cached != null) {
				cacheHitCount++;
				return cached.fork();
			}
			cacheMissCount++;
		}

		var result = new ComputedStyle();
		for (property in StyleProperty.all())
			result.set(property, property.defaultValue, frameworkSource);

		if (parent != null)
			for (property in StyleProperty.all())
				if (property.inherited && parent.has(property))
					result.set(property, parent.get(property), parent.source(property));

		applySheet(result, target, theme, false, "theme", environment);
		applySheet(result, target, application, false, "application", environment);
		applySheet(result, target, theme, true, "theme-state", environment);
		applySheet(result, target, application, true, "application-state", environment);
		applyLocal(result, local);
		applyTransitions(result, target, theme, application);
		if (cacheable) {
			if (cacheEntryCount >= MaxCacheEntries) {
				cache.clear();
				cacheEntryCount = 0;
			}
			cache.set(cacheKey, result.fork());
			cacheEntryCount++;
		}
		return result;
	}

	function hasTransitions(theme:Null<StyleSheet>, application:Null<StyleSheet>):Bool
		return (theme != null && theme.transitions.length > 0) ||
			(application != null && application.transitions.length > 0);

	function makeCacheKey(target:StyleTarget, parent:Null<ComputedStyle>,
			theme:Null<StyleSheet>, application:Null<StyleSheet>, local:Null<LayoutStyle>,
			environment:Null<StyleEnvironment>):String {
		return "target=" + targetKey(target) +
			"|parent=" + parentKey(parent) +
			"|theme=" + sheetKey(theme) +
			"|application=" + sheetKey(application) +
			"|local=" + localKey(local) +
			"|environment=" + (environment == null ? "none" : environment.identity + ":" + environment.revision);
	}

	static function sheetKey(sheet:Null<StyleSheet>):String
		return sheet == null ? "none" : sheet.identity + ":" + sheet.revision;

	static function targetKey(target:StyleTarget):String {
		return target.selectorFingerprint + "|states=" + target.states;
	}

	static function stringKey(value:Null<String>):String
		return value == null ? "-1:" : value.length + ":" + value;

	function parentKey(parent:Null<ComputedStyle>):String {
		if (parent == null)
			return "none";
		var result = "";
		for (property in StyleProperty.all()) {
			if (!property.inherited)
				continue;
			result += property.name + "=" + (parent.has(property) ? "present:" : "absent:") +
				valueKey(property, parent.get(property)) +
				"@" + sourceKey(parent.source(property)) + ";";
		}
		return result;
	}

	function localKey(local:Null<LayoutStyle>):String {
		if (local == null)
			return "none";
		var fingerprint = localFingerprint(local);
		var cached = localFingerprints.get(fingerprint);
		if (cached != null && sameLayoutStyle(local, cached.snapshot))
			return cached.key;
		var key = "local#" + nextLocalFingerprint++;
		if (localFingerprintCount >= MaxCacheEntries) {
			localFingerprints.clear();
			localFingerprintCount = 0;
		}
		localFingerprints.set(fingerprint, new StyleResolverLocalFingerprint(local.copy(), key));
		localFingerprintCount++;
		return key;
	}

	static function localFingerprint(local:LayoutStyle):Int {
		var result = 17;
		result = mix(result, Std.int(local.width.sizing));
		result = mix(result, floatFingerprint(local.width.value));
		result = mix(result, Std.int(local.height.sizing));
		result = mix(result, floatFingerprint(local.height.value));
		result = mix(result, Std.int(local.direction));
		result = mix(result, Std.int(local.childAlignX));
		result = mix(result, Std.int(local.childAlignY));
		result = mix(result, Std.int(local.childDistribution));
		result = mix(result, Std.int(local.positioning));
		result = mix(result, floatFingerprint(local.aspectRatio));
		result = mix(result, Std.int(local.wrapMode));
		result = mix(result, floatFingerprint(local.rowGap));
		result = mix(result, floatFingerprint(local.columnGap));
		result = mix(result, Std.int(local.alignSelf));
		result = mix(result, floatFingerprint(local.positionX));
		result = mix(result, floatFingerprint(local.positionY));
		result = mix(result, local.zIndex);
		result = mix(result, local.clipToParent ? 1 : 0);
		result = mix(result, floatFingerprint(local.padding.left));
		result = mix(result, floatFingerprint(local.padding.top));
		result = mix(result, floatFingerprint(local.padding.right));
		result = mix(result, floatFingerprint(local.padding.bottom));
		result = mix(result, floatFingerprint(local.childGap));
		result = colorFingerprint(result, local.background);
		result = mix(result, floatFingerprint(local.radiusTopLeft));
		result = mix(result, floatFingerprint(local.radiusTopRight));
		result = mix(result, floatFingerprint(local.radiusBottomRight));
		result = mix(result, floatFingerprint(local.radiusBottomLeft));
		result = mix(result, local.clipHorizontal ? 1 : 0);
		result = mix(result, local.clipVertical ? 1 : 0);
		result = mix(result, local.visible ? 1 : 0);
		result = mix(result, floatFingerprint(local.transform.a));
		result = mix(result, floatFingerprint(local.transform.b));
		result = mix(result, floatFingerprint(local.transform.c));
		result = mix(result, floatFingerprint(local.transform.d));
		result = mix(result, floatFingerprint(local.transform.tx));
		return mix(result, floatFingerprint(local.transform.ty));
	}

	static function colorFingerprint(seed:Int, value:Color):Int {
		if (value == null)
			return mix(seed, 0);
		var result = mix(seed, floatFingerprint(value.red));
		result = mix(result, floatFingerprint(value.green));
		result = mix(result, floatFingerprint(value.blue));
		return mix(result, floatFingerprint(value.alpha));
	}

	static function floatFingerprint(value:Float):Int {
		if (Math.isNaN(value))
			return 2143289344;
		if (!Math.isFinite(value))
			return value < 0.0 ? -2147483647 : 2147483647;
		var scaled = value * 1000003.0;
		if (scaled >= 2147483646.0)
			return 2147483646;
		if (scaled <= -2147483646.0)
			return -2147483646;
		return Std.int(scaled);
	}

	static inline function mix(seed:Int, value:Int):Int
		return seed * 31 + value;

	static function sameLayoutStyle(left:LayoutStyle, right:LayoutStyle):Bool
		return left.width.sizing == right.width.sizing && left.width.value == right.width.value &&
			left.height.sizing == right.height.sizing && left.height.value == right.height.value &&
			left.direction == right.direction && left.childAlignX == right.childAlignX &&
			left.childAlignY == right.childAlignY && left.childDistribution == right.childDistribution &&
			left.positioning == right.positioning && left.aspectRatio == right.aspectRatio &&
			left.wrapMode == right.wrapMode && left.rowGap == right.rowGap &&
			left.columnGap == right.columnGap && left.alignSelf == right.alignSelf &&
			left.positionX == right.positionX && left.positionY == right.positionY &&
			left.zIndex == right.zIndex && left.clipToParent == right.clipToParent &&
			left.padding.left == right.padding.left && left.padding.top == right.padding.top &&
			left.padding.right == right.padding.right && left.padding.bottom == right.padding.bottom &&
			left.childGap == right.childGap && sameColor(left.background, right.background) &&
			left.radiusTopLeft == right.radiusTopLeft && left.radiusTopRight == right.radiusTopRight &&
			left.radiusBottomRight == right.radiusBottomRight && left.radiusBottomLeft == right.radiusBottomLeft &&
			left.clipHorizontal == right.clipHorizontal && left.clipVertical == right.clipVertical &&
			left.visible == right.visible && sameTransform(left.transform, right.transform);

	static function sourceKey(source:Null<StyleSource>):String
		return source == null ? "none" : stringKey(source.stylesheet) + "|" + stringKey(source.selector) +
			"|" + source.rule + "|" + stringKey(source.layer);

	static function valueKey<T>(property:StyleProperty<T>, value:T):String {
		if (value == null)
			return "null";
		return switch property.name {
			case "background" | "borderColor" | "outlineColor" | "shadowColor" |
				"textColor" | "progressTrackColor" | "progressFillColor" |
				"sliderTrackColor" | "sliderFillColor" | "sliderThumbColor":
				colorKey(cast value);
			case "width" | "height":
				axisKey(cast value);
			case "padding":
				insetsKey(cast value);
			case "transform":
				transformKey(cast value);
			default:
				Std.string(value);
		};
	}

	static function colorKey(value:Color):String
		return value == null ? "null" : value.red + "," + value.green + "," + value.blue + "," + value.alpha;

	static function axisKey(value:LayoutAxis):String
		return value == null ? "null" : Std.string(value.sizing) + "," + value.value;

	static function insetsKey(value:Insets):String
		return value == null ? "null" : value.left + "," + value.top + "," + value.right + "," + value.bottom;

	static function transformKey(value:Transform2D):String
		return value == null ? "null" : value.a + "," + value.b + "," + value.c + "," +
			value.d + "," + value.tx + "," + value.ty;

	function applySheet(result:ComputedStyle, target:StyleTarget, sheet:Null<StyleSheet>,
			stateRules:Bool, layer:String, environment:Null<StyleEnvironment>):Void {
		if (sheet == null || sheet.isEmpty())
			return;
		var matches:Array<StyleRule> = [];
		for (rule in sheet.rules)
			if (rule.selector.hasState() == stateRules && rule.matches(target, environment))
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
		if (local.childDistribution != defaults.childDistribution)
			result.set(StyleProperty.ChildDistribution, local.childDistribution, source);
		if (local.positioning != defaults.positioning)
			result.set(StyleProperty.Positioning, local.positioning, source);
		if (local.aspectRatio != defaults.aspectRatio)
			result.set(StyleProperty.AspectRatio, local.aspectRatio, source);
		if (local.wrapMode != defaults.wrapMode)
			result.set(StyleProperty.WrapMode, local.wrapMode, source);
		if (local.rowGap != defaults.rowGap)
			result.set(StyleProperty.RowGap, local.rowGap, source);
		if (local.columnGap != defaults.columnGap)
			result.set(StyleProperty.ColumnGap, local.columnGap, source);
		if (local.alignSelf != defaults.alignSelf)
			result.set(StyleProperty.AlignSelf, local.alignSelf, source);
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

class StyleResolverLocalFingerprint {
	public final snapshot:LayoutStyle;
	public final key:String;

	public function new(snapshot:LayoutStyle, key:String) {
		this.snapshot = snapshot;
		this.key = key;
	}
}
