package nativekit.scene;

import NativeKitScene;
import haxe.io.Bytes;

/** Value builder for one scene-owned image asset. */
class ImageData {
    final value:nkscene_image_data;
    var pixels:Bytes = Bytes.alloc(0);

    public function new(width:Int, height:Int, format:Int = 2) {
        if (width <= 0 || height <= 0)
            throw "Image dimensions must be positive";
        value = new nkscene_image_data();
        value.set_struct_size(nkscene_image_data.size());
        value.set_width(width);
        value.set_height(height);
        value.set_format(format);
        value.set_mip_count(1);
    }

    public function setMipCount(count:Int):ImageData {
        if (count <= 0)
            throw "Image mip count must be positive";
        value.set_mip_count(count);
        return this;
    }

    public function setPixels(data:Bytes):ImageData {
        pixels = data;
        value.set_data_bytes(pixels);
        value.set_data_size(pixels.length);
        return this;
    }

    @:allow(Scene)
    function nativeValue():nkscene_image_data {
        value.set_data_bytes(pixels);
        value.set_data_size(pixels.length);
        return value;
    }
}
