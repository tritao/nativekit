package nativekit.audio;

/** Directional attenuation cone in radians and linear gain. */
class Cone {
	public final innerAngleRadians:Float;
	public final outerAngleRadians:Float;
	public final outerGain:Float;

	public function new(innerAngleRadians:Float, outerAngleRadians:Float, outerGain:Float) {
		this.innerAngleRadians = innerAngleRadians;
		this.outerAngleRadians = outerAngleRadians;
		this.outerGain = outerGain;
	}
}
