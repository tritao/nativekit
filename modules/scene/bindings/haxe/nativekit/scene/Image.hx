package nativekit.scene;

import NativeKitScene;

/** Owns one scene image asset. */
class Image {
    final scene:Scene;
    final value:nkscene_image_id;
    var disposed:Bool = false;

    @:allow(Scene)
    private function new(scene:Scene, value:nkscene_image_id) {
        this.scene = scene;
        this.value = value;
        scene.registerResource(dispose);
    }

    public function id():nkscene_image_id {
        ensureLive();
        return value;
    }

    public function dispose():Void {
        if (disposed)
            return;
        NativeKitScene.nkscene_image_destroy(scene.nativeHandle(), value);
        disposed = true;
    }

    public function isDisposed():Bool
        return disposed;

    @:allow(Scene)
    function setData(data:ImageData):Void {
        ensureLive();
        Scene.check(NativeKitScene.nkscene_image_set_data(scene.nativeHandle(), value,
            data.nativeValue()), "scene.setImageData");
    }

    function ensureLive():Void {
        if (disposed)
            throw "Image has been disposed";
        scene.ensureLive();
    }
}
