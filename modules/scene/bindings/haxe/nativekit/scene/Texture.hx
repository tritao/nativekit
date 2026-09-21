package nativekit.scene;

import NativeKitScene;

/** Owns one scene texture asset. */
class Texture {
    final scene:Scene;
    final value:nkscene_texture_id;
    var disposed:Bool = false;

    @:allow(Scene)
    private function new(scene:Scene, value:nkscene_texture_id) {
        this.scene = scene;
        this.value = value;
        scene.registerResource(dispose);
    }

    public function id():nkscene_texture_id {
        ensureLive();
        return value;
    }

    public function dispose():Void {
        if (disposed)
            return;
        NativeKitScene.nkscene_texture_destroy(scene.nativeHandle(), value);
        disposed = true;
    }

    public function isDisposed():Bool
        return disposed;

    @:allow(Scene)
    function setData(data:TextureData):Void {
        ensureLive();
        Scene.check(NativeKitScene.nkscene_texture_set_data(scene.nativeHandle(), value,
            data.nativeValue()), "scene.setTextureData");
    }

    function ensureLive():Void {
        if (disposed)
            throw "Texture has been disposed";
        scene.ensureLive();
    }
}
