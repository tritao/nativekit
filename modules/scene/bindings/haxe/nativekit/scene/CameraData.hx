package nativekit.scene;

import NativeKitScene;

/** Value builder for a perspective or orthographic scene camera. */
class CameraData {
    final value:nkscene_camera_data;

    public function new() {
        value = new nkscene_camera_data();
        value.set_struct_size(nkscene_camera_data.size());
        value.set_projection(1);
        value.set_fov_y(Math.PI / 3.0);
        value.set_orthographic_height(10.0);
        value.set_near_plane(0.01);
        value.set_far_plane(1000.0);
        value.set_aspect_ratio(0.0);
    }

    public function setPerspective(fovY:Float, nearPlane:Float, farPlane:Float,
            aspectRatio:Float = 0.0):CameraData {
        value.set_projection(1);
        value.set_fov_y(fovY);
        value.set_near_plane(nearPlane);
        value.set_far_plane(farPlane);
        value.set_aspect_ratio(aspectRatio);
        return this;
    }

    public function setOrthographic(height:Float, nearPlane:Float, farPlane:Float,
            aspectRatio:Float = 0.0):CameraData {
        value.set_projection(2);
        value.set_orthographic_height(height);
        value.set_near_plane(nearPlane);
        value.set_far_plane(farPlane);
        value.set_aspect_ratio(aspectRatio);
        return this;
    }

    @:allow(Scene)
    function nativeValue():nkscene_camera_data
        return value;
}
