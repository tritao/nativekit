package nativekit.scene;

import NativeKitScene;

/** Owns one scene sampler asset. */
class Sampler {
    final scene:Scene;
    final value:nkscene_sampler_id;
    var disposed:Bool = false;

    @:allow(Scene)
    private function new(scene:Scene, value:nkscene_sampler_id) {
        this.scene = scene;
        this.value = value;
        scene.registerResource(dispose);
    }

    public function id():nkscene_sampler_id {
        ensureLive();
        return value;
    }

    public function dispose():Void {
        if (disposed)
            return;
        NativeKitScene.nkscene_sampler_destroy(scene.nativeHandle(), value);
        disposed = true;
    }

    public function isDisposed():Bool
        return disposed;

    @:allow(Scene)
    function setData(data:SamplerData):Void {
        ensureLive();
        Scene.check(NativeKitScene.nkscene_sampler_set_data(scene.nativeHandle(), value,
            data.nativeValue()), "scene.setSamplerData");
    }

    function ensureLive():Void {
        if (disposed)
            throw "Sampler has been disposed";
        scene.ensureLive();
    }
}
