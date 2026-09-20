package nativekit.gpu;

/** Stable Haxe names for the integer enums in the GPU adapter ABI. */
enum abstract BufferUsage(Int) to Int {
	var Vertex = 1;
	var Index = 2;
	var Storage = 4;
	var Uniform = 8;
	var Transfer = 16;
}

enum abstract ImageFormat(Int) to Int {
	var R8 = 1;
	var Rgba8 = 2;
	var Rg8 = 3;
	var Bgra8 = 4;
	var R16f = 5;
	var Rg16f = 6;
	var Rgba16f = 7;
	var R32f = 8;
	var Rgba32f = 9;
	var R32Uint = 10;
	var Depth16 = 11;
	var Depth24Stencil8 = 12;
	var Depth32f = 13;
}

enum abstract ImageUsage(Int) to Int {
	var Sampled = 1;
	var RenderTarget = 2;
	var DepthStencil = 4;
	var Storage = 8;
}

enum abstract LoadAction(Int) to Int {
	var Load = 1;
	var Clear = 2;
	var Discard = 3;
}

enum abstract StoreAction(Int) to Int {
	var Store = 1;
	var Discard = 2;
}

enum abstract Filter(Int) to Int {
	var Nearest = 1;
	var Linear = 2;
}

enum abstract Wrap(Int) to Int {
	var Repeat = 1;
	var ClampToEdge = 2;
}

enum abstract IndexType(Int) to Int {
	var None = 0;
	var UInt16 = 1;
	var UInt32 = 2;
}

enum abstract ShaderStage(Int) to Int {
	var Vertex = 1;
	var Fragment = 2;
	var Compute = 3;
}

enum abstract ShaderLanguage(Int) to Int {
	var Glsl = 1;
	var Hlsl5 = 2;
	var Msl = 3;
}

enum abstract UniformType(Int) to Int {
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

enum abstract VertexFormat(Int) to Int {
	var Float = 1;
	var Float2 = 2;
	var Float3 = 3;
	var Float4 = 4;
}
