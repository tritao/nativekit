package nativekit.scene;

import NativeKitScene;

/** One occurrence transform in a bulk transaction update. */
class TransformUpdate {
    final value:nkscene_transform_update;
    public function new(occurrence:Occurrence, transform:Transform) {
        value = new nkscene_transform_update();
        value.set_occurrence(occurrence.nativeValue());
        value.set_transform(transform.nativeValue());
    }

    @:allow(Transaction)
    function nativeValue():nkscene_transform_update
        return value;
}
