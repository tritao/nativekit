/** Six-value affine transform using x'=a*x+c*y+tx and y'=b*x+d*y+ty. */
class Transform2D {
	static inline var InvertibilityEpsilon:Float = 0.000001;

	public final a:Float;
	public final b:Float;
	public final c:Float;
	public final d:Float;
	public final tx:Float;
	public final ty:Float;

	public function new(a:Float, b:Float, c:Float, d:Float, tx:Float, ty:Float) {
		this.a = a;
		this.b = b;
		this.c = c;
		this.d = d;
		this.tx = tx;
		this.ty = ty;
	}

	public static function identity():Transform2D
		return new Transform2D(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);

	public static function translation(x:Float, y:Float):Transform2D
		return new Transform2D(1.0, 0.0, 0.0, 1.0, x, y);

	public static function scale(x:Float, y:Float):Transform2D
		return new Transform2D(x, 0.0, 0.0, y, 0.0, 0.0);

	public static function rotation(radians:Float):Transform2D {
		var cosine = Math.cos(radians);
		var sine = Math.sin(radians);
		return new Transform2D(cosine, sine, -sine, cosine, 0.0, 0.0);
	}

	/** Returns this transform followed by `other` in column-vector notation. */
	public function multiply(other:Transform2D):Transform2D {
		if (other == null)
			throw "Transform multiplication requires another transform";
		return new Transform2D(
			a * other.a + c * other.b,
			b * other.a + d * other.b,
			a * other.c + c * other.d,
			b * other.c + d * other.d,
			a * other.tx + c * other.ty + tx,
			b * other.tx + d * other.ty + ty);
	}

	/** Alias for multiply, useful when composing a local transform chain. */
	public function concat(other:Transform2D):Transform2D
		return multiply(other);

	public function determinant():Float
		return a * d - b * c;

	public function isInvertible():Bool {
		var value = determinant();
		return Math.isFinite(value) && Math.abs(value) >= InvertibilityEpsilon &&
			Math.isFinite(a) && Math.isFinite(b) && Math.isFinite(c) &&
			Math.isFinite(d) && Math.isFinite(tx) && Math.isFinite(ty);
	}

	public function tryInverse():Null<Transform2D> {
		if (!isInvertible())
			return null;
		var reciprocal = 1.0 / determinant();
		return new Transform2D(d * reciprocal, -b * reciprocal, -c * reciprocal,
			a * reciprocal, (c * ty - d * tx) * reciprocal,
			(b * tx - a * ty) * reciprocal);
	}

	public function inverse():Transform2D {
		var result = tryInverse();
		if (result == null)
			throw "Transform is not invertible";
		return result;
	}

	public function transformPoint(point:Point):Point {
		if (point == null)
			throw "Transform points cannot be null";
		return new Point(a * point.x + c * point.y + tx,
			b * point.x + d * point.y + ty);
	}

	public function transformVector(vector:Point):Point {
		if (vector == null)
			throw "Transform vectors cannot be null";
		return new Point(a * vector.x + c * vector.y,
			b * vector.x + d * vector.y);
	}

	public function translated(x:Float, y:Float):Transform2D
		return multiply(Transform2D.translation(x, y));

	public function scaled(x:Float, y:Float):Transform2D
		return multiply(Transform2D.scale(x, y));

	public function rotated(radians:Float):Transform2D
		return multiply(Transform2D.rotation(radians));

	public function skewed(xRadians:Float, yRadians:Float):Transform2D {
		var x = Math.tan(xRadians), y = Math.tan(yRadians);
		return new Transform2D(a + c * y, b + d * y, a * x + c, b * x + d, tx, ty);
	}
}
