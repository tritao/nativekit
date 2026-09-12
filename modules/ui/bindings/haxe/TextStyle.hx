import FontFamily;

/** Semantic font and inline-spacing inputs shared by all NativeKit text APIs. */
class TextStyle {
    public var font:FontFamily;
    public var fontSize:Float;
    public var letterSpacing:Float;

    public function new(fontSize:Float = 16.0, font:FontFamily = FontFamily.Default,
            letterSpacing:Float = 0.0) {
        if (fontSize <= 0.0 || Math.isNaN(fontSize) || Math.isNaN(letterSpacing))
            throw "Text style values are invalid";
        this.font = font;
        this.fontSize = fontSize;
        this.letterSpacing = letterSpacing;
    }
}
