package nativekit.scene;

import NativeKitScene;

/** One occurrence transform in a bulk transaction update. */
class TransformUpdate {
    final value:nkscene_transform_update;
    final occurrenceValue:Occurrence;
    final transformValue:Transform;

    public function new(occurrence:Occurrence, transform:Transform) {
        occurrenceValue = occurrence;
        transformValue = transform;
        value = new nkscene_transform_update();
        value.set_occurrence(occurrence.nativeValue());
        value.set_transform(transform.nativeValue());
    }

    @:allow(Transaction)
    function nativeValue():nkscene_transform_update
        return value;

    @:allow(Transaction)
    function apply(transaction:Transaction):Void
        transaction.setTransform(occurrenceValue, transformValue);
}
