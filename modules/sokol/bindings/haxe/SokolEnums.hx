/** Stable Haxe names for the integer enums in the Sokol adapter ABI. */
enum abstract SokolBufferUsage(Int) to Int {
	var Vertex = 1;
	var Index = 2;
}

enum abstract SokolFilter(Int) to Int {
	var Nearest = 1;
	var Linear = 2;
}

enum abstract SokolWrap(Int) to Int {
	var Repeat = 1;
	var ClampToEdge = 2;
}

enum abstract SokolIndexType(Int) to Int {
	var None = 0;
	var UInt16 = 1;
	var UInt32 = 2;
}

enum abstract SokolShaderStage(Int) to Int {
	var Vertex = 1;
	var Fragment = 2;
}

enum abstract SokolUniformType(Int) to Int {
	var Float = 1;
	var Float2 = 2;
	var Float3 = 3;
	var Float4 = 4;
	var Int = 5;
	var Int2 = 6;
	var Int3 = 7;
	var Int4 = 8;
	var Mat4 = 9;
}

enum abstract SokolVertexFormat(Int) to Int {
	var Float = 1;
	var Float2 = 2;
	var Float3 = 3;
	var Float4 = 4;
}
