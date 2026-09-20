package nativekit.scene;

import NativeKitScene;

/** Small value builder for column-major scene transforms. */
class Transform {
	final value:nkscene_transform;

	public function new() {
		value = new nkscene_transform();
		resetIdentity();
	}

	public static function identity():Transform
		return new Transform();

	@:allow(OccurrenceInfo)
	static function fromNative(value:nkscene_transform):Transform {
		var result = new Transform();
		for (index in 0...16)
			result.value.set_matrix(index, value.get_matrix(index));
		return result;
	}

	public function set(index:Int, element:Float):Transform {
		if (index < 0 || index >= 16)
			throw "Transform matrix index must be between 0 and 15";
		value.set_matrix(index, element);
		return this;
	}

	public function identityInPlace():Transform {
		for (index in 0...16)
			value.set_matrix(index, index % 5 == 0 ? 1.0 : 0.0);
		return this;
	}

	public function translated(x:Float, y:Float, z:Float):Transform {
		value.set_matrix(12, x);
		value.set_matrix(13, y);
		value.set_matrix(14, z);
		return this;
	}

	public function element(index:Int):Float {
		if (index < 0 || index >= 16)
			throw "Transform matrix index must be between 0 and 15";
		return value.get_matrix(index);
	}

	@:allow(Transaction, SceneView)
	function nativeValue():nkscene_transform
		return value;

	private function resetIdentity():Void
		identityInPlace();
}
