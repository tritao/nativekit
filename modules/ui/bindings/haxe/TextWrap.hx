/** Paragraph line-breaking policy. */
enum abstract TextWrap(Int) from Int to Int {
	var None = 0;
	var Word = 1;
	var WordCharacter = 2;
}
