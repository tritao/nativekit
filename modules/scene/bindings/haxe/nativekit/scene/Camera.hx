package nativekit.scene;

import NativeKitScene;

/** Owns one scene camera asset. */
class Camera {
    final scene:Scene;
    final value:nkscene_camera_id;
    var disposed:Bool = false;

    @:allow(Scene)
    private function new(scene:Scene, value:nkscene_camera_id) {
        this.scene = scene;
        this.value = value;
        scene.registerResource(dispose);
    }

    public function id():nkscene_camera_id {
        ensureLive();
        return value;
    }

    public function dispose():Void {
        if (disposed)
            return;
        NativeKitScene.nkscene_camera_destroy(scene.nativeHandle(), value);
        disposed = true;
    }

    public function isDisposed():Bool
        return disposed;

    @:allow(Scene)
    function setData(data:CameraData):Void {
        ensureLive();
        Scene.check(NativeKitScene.nkscene_camera_set_data(scene.nativeHandle(), value,
            data.nativeValue()), "scene.setCameraData");
    }

    function ensureLive():Void {
        if (disposed)
            throw "Camera has been disposed";
        scene.ensureLive();
    }
}
