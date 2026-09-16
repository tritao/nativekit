package nativekit.ui.debug;

import Rect;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.StyleSource;
import nativekit.ui.style.StyleInspectionEntry;
import nativekit.ui.style.InkOverflow;

/** Grouped effect diagnostics carried by a node inspection snapshot. */
class UiNodeEffectInfo {
	public final effectDescriptions:Array<String>;
	public final backdropEffectDescriptions:Array<String>;
	public final backdropEffectPasses:Int;
	public final maskPasses:Int;
	public final isolationReasons:Array<String>;
	public final estimatedIntermediateTargets:Int;

	public function new(effectDescriptions:Array<String> = null,
			backdropEffectDescriptions:Array<String> = null, backdropEffectPasses:Int = 0,
			maskPasses:Int = 0, isolationReasons:Array<String> = null,
			estimatedIntermediateTargets:Int = 0) {
		this.effectDescriptions = effectDescriptions == null ? [] : effectDescriptions.copy();
		this.backdropEffectDescriptions = backdropEffectDescriptions == null
			? [] : backdropEffectDescriptions.copy();
		this.backdropEffectPasses = backdropEffectPasses;
		this.maskPasses = maskPasses;
		this.isolationReasons = isolationReasons == null ? [] : isolationReasons.copy();
		this.estimatedIntermediateTargets = estimatedIntermediateTargets;
	}
}

/** Stable headless inspection record for one resolved Haxe render node. */
class UiNodeSnapshot {
	public final id:Int;
	public final parentId:Int;
	public final depth:Int;
	public final visualKind:Int;
	public final bounds:Rect;
	public final clipBounds:Rect;
	public final contentBounds:Rect;
	public final visible:Bool;
	public final enabled:Bool;
	public final focusable:Bool;
	public final focused:Bool;
	public final hovered:Bool;
	public final pressed:Bool;
	public final interactionStates:Int;
	public final styleType:Null<String>;
	public final computedStyle:Null<ComputedStyle>;
	public final styleEntries:Array<StyleInspectionEntry>;
	public final matchingStyleRules:Array<StyleSource>;
	/** True when opacity, effects, backdrop effects, or a mask require isolation. */
	public final causesIsolation:Bool;
	/** Logical ink expansion requested by subtree effects. */
	public final inkOverflow:InkOverflow;
	/** Logical paint bounds after applying ink overflow. */
	public final paintBounds:Rect;
	/** Estimated sampled effect passes for this node and its backdrop. */
	public final effectPasses:Int;
	/** Descriptions of foreground effects in application order. */
	public final effectDescriptions:Array<String>;
	/** Descriptions of effects applied to the backdrop input. */
	public final backdropEffectDescriptions:Array<String>;
	/** Estimated effect passes applied to the backdrop input. */
	public final backdropEffectPasses:Int;
	/** Number of mask composition passes estimated for this node. */
	public final maskPasses:Int;
	/** Reasons this node requires an isolated layer. */
	public final isolationReasons:Array<String>;
	/** Estimated number of intermediate RGBA targets used by this node. */
	public final estimatedIntermediateTargets:Int;
	/** Logical RGBA bytes estimated for the node's intermediate targets. */
	public final estimatedRenderTargetBytes:Float;
	public final zIndex:Int;
	public final role:Int;
	public final semanticStates:Int;
	public final label:Null<String>;
	public final value:Null<String>;
	public final actions:Int;

	public function new(id:Int, parentId:Int, depth:Int, visualKind:Int, bounds:Rect,
			clipBounds:Rect, contentBounds:Rect, visible:Bool, enabled:Bool,
			focusable:Bool, focused:Bool, hovered:Bool, pressed:Bool, zIndex:Int, role:Int,
			semanticStates:Int, label:Null<String>, value:Null<String>, actions:Int,
			interactionStates:Int = 0, styleType:Null<String> = null,
			computedStyle:Null<ComputedStyle> = null,
			?styleEntries:Array<StyleInspectionEntry>, ?matchingStyleRules:Array<StyleSource>,
			causesIsolation:Bool = false, ?inkOverflow:InkOverflow, ?paintBounds:Rect,
			effectPasses:Int = 0, estimatedRenderTargetBytes:Float = 0.0,
			?effectInfo:UiNodeEffectInfo) {
		this.id = id;
		this.parentId = parentId;
		this.depth = depth;
		this.visualKind = visualKind;
		this.bounds = bounds;
		this.clipBounds = clipBounds;
		this.contentBounds = contentBounds;
		this.visible = visible;
		this.enabled = enabled;
		this.focusable = focusable;
		this.focused = focused;
		this.hovered = hovered;
		this.pressed = pressed;
		this.interactionStates = interactionStates;
		this.styleType = styleType;
		this.computedStyle = computedStyle;
		this.styleEntries = styleEntries == null ? [] : styleEntries;
		this.matchingStyleRules = matchingStyleRules == null ? [] : matchingStyleRules;
		this.causesIsolation = causesIsolation;
		this.inkOverflow = inkOverflow == null ? InkOverflow.zero() : inkOverflow;
		this.paintBounds = paintBounds == null ? bounds : paintBounds;
		this.effectPasses = effectPasses;
		this.effectDescriptions = effectInfo == null ? [] : effectInfo.effectDescriptions;
		this.backdropEffectDescriptions = effectInfo == null ? [] : effectInfo.backdropEffectDescriptions;
		this.backdropEffectPasses = effectInfo == null ? 0 : effectInfo.backdropEffectPasses;
		this.maskPasses = effectInfo == null ? 0 : effectInfo.maskPasses;
		this.isolationReasons = effectInfo == null ? [] : effectInfo.isolationReasons;
		this.estimatedIntermediateTargets = effectInfo == null ? 0 : effectInfo.estimatedIntermediateTargets;
		this.estimatedRenderTargetBytes = estimatedRenderTargetBytes;
		this.zIndex = zIndex;
		this.role = role;
		this.semanticStates = semanticStates;
		this.label = label;
		this.value = value;
		this.actions = actions;
	}
}
