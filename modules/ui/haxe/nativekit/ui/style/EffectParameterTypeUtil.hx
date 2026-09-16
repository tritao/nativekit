package nativekit.ui.style;

/** Operations kept separate because Haxeon treats enum abstracts as constants. */
class EffectParameterTypeUtil {
	public static function componentCount(type:EffectParameterType):Int {
		return switch type {
			case EffectParameterType.Float: 1;
			case EffectParameterType.Vec2: 2;
			case EffectParameterType.Vec4 | EffectParameterType.Color: 4;
			default: 0;
		};
	}
}
