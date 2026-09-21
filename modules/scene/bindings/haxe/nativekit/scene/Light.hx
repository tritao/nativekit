package nativekit.scene;

import NativeKitScene;

/** Owns one scene light asset. */
class Light {
    final scene:Scene;
    final value:nkscene_light_id;
    var disposed:Bool = false;

    @:allow(Scene)
    private function new(scene:Scene, value:nkscene_light_id) {
        this.scene = scene;
        this.value = value;
        scene.registerResource(dispose);
    }

    public function id():nkscene_light_id {
        ensureLive();
        return value;
    }

    public function dispose():Void {
        if (disposed)
            return;
        NativeKitScene.nkscene_light_destroy(scene.nativeHandle(), value);
        disposed = true;
    }

    public function isDisposed():Bool
        return disposed;

    @:allow(Scene)
    function setData(data:LightData):Void {
        ensureLive();
        Scene.check(NativeKitScene.nkscene_light_set_data(scene.nativeHandle(), value,
            data.nativeValue()), "scene.setLightData");
    }

    function ensureLive():Void {
        if (disposed)
            throw "Light has been disposed";
        scene.ensureLive();
    }
}
