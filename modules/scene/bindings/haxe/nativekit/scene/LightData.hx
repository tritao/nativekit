package nativekit.scene;

import NativeKitScene;

/** Value builder for a directional, point, or spot scene light. */
class LightData {
    final value:nkscene_light_data;

    public function new() {
        value = new nkscene_light_data();
        value.set_struct_size(nkscene_light_data.size());
        value.set_type(1);
        setColor(1.0, 1.0, 1.0);
        value.set_intensity(1.0);
        value.set_range(10.0);
        value.set_inner_cone_angle(Math.PI / 12.0);
        value.set_outer_cone_angle(Math.PI / 4.0);
    }

    public function setType(type:Int):LightData {
        value.set_type(type);
        return this;
    }

    public function setColor(red:Float, green:Float, blue:Float):LightData {
        value.set_color(0, red);
        value.set_color(1, green);
        value.set_color(2, blue);
        return this;
    }

    public function setIntensity(intensity:Float):LightData {
        value.set_intensity(intensity);
        return this;
    }

    public function setRange(range:Float):LightData {
        value.set_range(range);
        return this;
    }

    public function setCone(innerAngle:Float, outerAngle:Float):LightData {
        value.set_inner_cone_angle(innerAngle);
        value.set_outer_cone_angle(outerAngle);
        return this;
    }

    @:allow(Scene)
    function nativeValue():nkscene_light_data
        return value;
}
