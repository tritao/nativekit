package nativekit.ui.core;

/** Ephemeral description rebuilt to produce a render tree each frame. */
interface View {
	function build(context:BuildContext):RenderNode;
}
