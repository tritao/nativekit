package nativekit.scene;

import NativeKitScene;
import haxe.io.Bytes;

/** Owns the backing arrays used to describe one geometry resource. */
class GeometryData {
	final value:nkscene_geometry_data;
	final vertices:Array<nkscene_geometry_vertex> = [];
	final subelements:Array<nkscene_subelement_range> = [];
	final streams:Array<nkscene_vertex_stream> = [];
	final streamData:Array<Bytes> = [];
	final indexValues:Array<Int> = [];
	var indices:Bytes = Bytes.alloc(0);

	public function new() {
		value = new nkscene_geometry_data();
		value.set_struct_size(nkscene_geometry_data.size());
	}

	public function addVertex(x:Float, y:Float, z:Float):Int {
		var vertex = new nkscene_geometry_vertex();
		vertex.set_position(0, x);
		vertex.set_position(1, y);
		vertex.set_position(2, z);
		vertices.push(vertex);
		return vertices.length - 1;
	}

	public function addTriangle(first:Int, second:Int, third:Int):GeometryData {
		appendIndex(first);
		appendIndex(second);
		appendIndex(third);
		return this;
	}

	public function setPrimitiveType(primitiveType:Int):GeometryData {
		value.set_primitive_type(primitiveType);
		return this;
	}

	/** Adds one tightly packed or explicitly strided vertex attribute stream. */
	public function addStream(semantic:Int, format:Int, data:Bytes, count:Int,
			stride:Int = 0):GeometryData {
		if (count < 0 || stride < 0)
			throw "Geometry stream count and stride must be non-negative";
		var stream = new nkscene_vertex_stream();
		stream.set_struct_size(nkscene_vertex_stream.size());
		stream.set_semantic(semantic);
		stream.set_format(format);
		stream.set_stride(stride);
		stream.set_data_bytes(data);
		stream.set_count(count);
		streams.push(stream);
		streamData.push(data);
		return this;
	}

	public function setBounds(minX:Float, minY:Float, minZ:Float,
			maxX:Float, maxY:Float, maxZ:Float):GeometryData {
		var bounds = new nkscene_bounds();
		bounds.set_minimum(0, minX);
		bounds.set_minimum(1, minY);
		bounds.set_minimum(2, minZ);
		bounds.set_maximum(0, maxX);
		bounds.set_maximum(1, maxY);
		bounds.set_maximum(2, maxZ);
		bounds.set_valid(1);
		value.set_bounds(bounds);
		return this;
	}

	public function addSubelement(firstPrimitive:Int, primitiveCount:Int,
			subelement:Int):GeometryData {
		var range = new nkscene_subelement_range();
		range.set_first_primitive(firstPrimitive);
		range.set_primitive_count(primitiveCount);
		range.set_subelement(subelement);
		subelements.push(range);
		return this;
	}

	public function vertexCount():Int
		return vertices.length;

	public function triangleCount():Int
		return Std.int(indexValues.length / 3);

	@:allow(Scene)
	function nativeValue():nkscene_geometry_data {
		value.set_vertices(vertices);
		if (indexValues.length != 0) {
			indices = Bytes.alloc(indexValues.length * 4);
			for (index in 0...indexValues.length)
				indices.setInt32(index * 4, indexValues[index]);
			value.set_indices_bytes(indices);
			value.set_index_count(indexValues.length);
		}
		value.set_subelements(subelements);
		if (streams.length != 0) {
			value.set_streams(streams);
			value.set_stream_count(streams.length);
		}
		return value;
	}

	function appendIndex(index:Int):Void {
		if (index < 0 || index > 0x7fffffff)
			throw "Geometry index must be a non-negative 31-bit integer";
		indexValues.push(index);
	}
}
