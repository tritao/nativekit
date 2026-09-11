/** Six-value affine transform using x'=a*x+c*y+tx and y'=b*x+d*y+ty. */
class Transform2D {
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

	public function translated(x:Float, y:Float):Transform2D
		return new Transform2D(a, b, c, d, a * x + c * y + tx, b * x + d * y + ty);

	public function scaled(x:Float, y:Float):Transform2D
		return new Transform2D(a * x, b * x, c * y, d * y, tx, ty);

	public function rotated(radians:Float):Transform2D {
		var cosine = Math.cos(radians), sine = Math.sin(radians);
		return new Transform2D(a * cosine + c * sine, b * cosine + d * sine,
			-a * sine + c * cosine, -b * sine + d * cosine, tx, ty);
	}

	public function skewed(xRadians:Float, yRadians:Float):Transform2D {
		var x = Math.tan(xRadians), y = Math.tan(yRadians);
		return new Transform2D(a + c * y, b + d * y, a * x + c, b * x + d, tx, ty);
	}
}
