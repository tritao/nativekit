package nativekit.gpu;

import nativekit.ffi.NativeKitGpu;

/** One color attachment and its optional MSAA resolve image. */
class ColorAttachment {
	public final image:Image;
	public var resolveImage:Null<Image>;
	public var action:AttachmentAction;

	public function new(image:Image, action:AttachmentAction,
		resolveImage:Null<Image> = null) {
		this.image = image;
		this.action = action;
		this.resolveImage = resolveImage;
	}
}

/** Portable render-pass attachment description. */
class RenderPassDesc {
	public var colors:Array<ColorAttachment>;
	public var depthStencil:Null<Image>;
	public var depthStencilAction:AttachmentAction;

	public function new() {
		colors = [];
		depthStencil = null;
		depthStencilAction = new AttachmentAction();
	}

	public function color(image:Image, action:AttachmentAction,
		resolveImage:Null<Image> = null):RenderPassDesc {
		if (colors.length >= 4)
			throw "GPU render passes support at most four color attachments";
		colors.push(new ColorAttachment(image, action, resolveImage));
		return this;
	}

	public function depth(image:Image, action:AttachmentAction = null):RenderPassDesc {
		depthStencil = image;
		depthStencilAction = action == null ? new AttachmentAction() : action;
		return this;
	}

	@:allow(Renderer)
	function nativeValue():nkgpu_render_pass_desc {
		var value = new nkgpu_render_pass_desc();
		value.set_struct_size(204);
		value.set_color_count(colors.length);
		for (index in 0...colors.length) {
			var source = colors[index];
			var attachment = new nkgpu_color_attachment();
			attachment.set_image(source.image.nativeHandle());
			attachment.set_resolve_image(source.resolveImage == null ? nkgpu_image.invalid() : source.resolveImage.nativeHandle());
			attachment.set_action(source.action.nativeValue());
			value.set_colors(index, attachment);
		}
		if (depthStencil != null) {
			value.set_depth_stencil(depthStencil.nativeHandle());
			value.set_depth_stencil_action(depthStencilAction.nativeValue());
		}
		return value;
	}
}
