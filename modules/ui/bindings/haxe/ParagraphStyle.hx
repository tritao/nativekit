/** Semantic paragraph inputs shared by all NativeKit text APIs. */
class ParagraphStyle {
    public var wrap:TextWrap;
    public var alignment:TextAlignment;
    public var lineHeight:Null<Float>;
    public var direction:TextDirection;

    public function new(wrap:TextWrap = TextWrap.WordCharacter,
            alignment:TextAlignment = TextAlignment.Start, lineHeight:Null<Float> = null,
            direction:TextDirection = TextDirection.Auto) {
        this.wrap = wrap;
        this.alignment = alignment;
        this.lineHeight = lineHeight;
        this.direction = direction;
    }
}
