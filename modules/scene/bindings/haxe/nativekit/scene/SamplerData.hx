package nativekit.scene;

import NativeKitScene;

/** Value builder for scene sampler state. */
class SamplerData {
    final value:nkscene_sampler_data;

    public function new() {
        value = new nkscene_sampler_data();
        value.set_struct_size(nkscene_sampler_data.size());
        value.set_min_filter(2);
        value.set_mag_filter(2);
        value.set_wrap_u(1);
        value.set_wrap_v(1);
        value.set_wrap_w(1);
        value.set_max_anisotropy(1.0);
    }

    public function setFilters(minFilter:Int, magFilter:Int):SamplerData {
        value.set_min_filter(minFilter);
        value.set_mag_filter(magFilter);
        return this;
    }

    public function setWrap(wrapU:Int, wrapV:Int, wrapW:Int = 1):SamplerData {
        value.set_wrap_u(wrapU);
        value.set_wrap_v(wrapV);
        value.set_wrap_w(wrapW);
        return this;
    }

    public function setMaxAnisotropy(value:Float):SamplerData {
        if (value < 1.0)
            throw "Sampler anisotropy must be at least one";
        this.value.set_max_anisotropy(value);
        return this;
    }

    @:allow(Scene)
    function nativeValue():nkscene_sampler_data
        return value;
}
