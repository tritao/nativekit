package nativekit.ui.style;

import nativekit.ui.animation.Easing;

/** Ordered collection of typed style rules. */
class StyleSheet {
	public final name:String;
	public final rules(default, null):Array<StyleRule>;
	public final transitions(default, null):Array<StyleTransition>;

	public function new(?name:String) {
		this.name = name == null || name.length == 0 ? "StyleSheet" : name;
		rules = [];
		transitions = [];
	}

	public function rule(selector:StyleSelector, declarations:Array<StyleValue>):StyleRule {
		var result = new StyleRule(name, selector, declarations, rules.length);
		rules.push(result);
		return result;
	}

	public function when(condition:EnvironmentCondition, selector:StyleSelector,
			declarations:Array<StyleValue>):StyleRule {
		if (condition == null)
			throw "Conditional style rules require an environment condition";
		var result = new StyleRule(name, selector, declarations, rules.length, condition);
		rules.push(result);
		return result;
	}

	public function clear():Void
		{
			rules.resize(0);
			transitions.resize(0);
		}

	public function isEmpty():Bool
		return rules.length == 0;

	public function transition<T>(property:StyleProperty<T>, duration:Float,
			easing:Int = Easing.EaseOut):StyleTransition {
		var result = new StyleTransition(property, duration, easing);
		transitions.push(result);
		return result;
	}

	public function transitionFor(property:StyleProperty<Dynamic>):Null<StyleTransition> {
		for (transition in transitions)
			if (transition.property.name == property.name)
				return transition;
		return null;
	}
}
