import NativeKit.GraphicsApi;
import NativeKit.WindowOptions;
import NativeKit.WindowFlags;
import NativeKit.WindowKind;
import NativeKitEventValue;
import NativeKitRuntime;
import NativeKitWindow;
import NativeKitGpu;
import GraphicsImageRef;
import nativekit.gpu.Buffer;
import nativekit.gpu.CommandBuffer;
import nativekit.gpu.Enums.Filter;
import nativekit.gpu.Enums.IndexType;
import nativekit.gpu.Enums.ShaderStage;
import nativekit.gpu.Enums.ShaderLanguage;
import nativekit.gpu.Enums.UniformType;
import nativekit.gpu.Enums.VertexFormat;
import nativekit.gpu.Enums.Wrap;
import nativekit.gpu.Image;
import nativekit.gpu.Pipeline;
import nativekit.gpu.RenderTarget;
import nativekit.gpu.Renderer;
import nativekit.gpu.Sampler;
import nativekit.gpu.Shader;
import nativekit.gpu.Surface;
import haxe.io.Bytes;

class Triangle {
	static function verifyGeneratedEnumMembers():Void {
		var vertexFormat:NativeKitGpu.VertexFormat = NativeKitGpu.VertexFormat.Float;
		var floatUniform:NativeKitGpu.UniformType = NativeKitGpu.UniformType.Float;
		var intUniform:NativeKitGpu.UniformType = NativeKitGpu.UniformType.Int;
		if (vertexFormat != NativeKitGpu.VertexFormat.Float || floatUniform != NativeKitGpu.UniformType.Float ||
			intUniform != NativeKitGpu.UniformType.Int)
			throw "generated GPU enum members changed values";
	}

	static function createVertexBuffer(renderer:Renderer):Buffer {
		var values = [
			-0.04, 0.04, 1.0, 0.30, 0.35, 0.0, 0.0,
			0.04, 0.04, 0.25, 0.85, 0.55, 2.0, 0.0,
			0.04, -0.04, 0.30, 0.55, 1.0, 2.0, 2.0,
			-0.04, -0.04, 1.0, 0.85, 0.25, 0.0, 2.0
		];
		return Buffer.vertexFloats(renderer, values);
	}

	static function createIndexBuffer(renderer:Renderer):Buffer
		return Buffer.indices16(renderer, [0, 1, 2, 0, 2, 3]);

	static function createCheckerboard(renderer:Renderer):Image {
		var pixels = Bytes.alloc(8 * 8 * 4);
		for (y in 0...8) {
			for (x in 0...8) {
				var bright = ((x + y) & 1) == 0;
				var channel = bright ? 240 : 35;
				var offset = (y * 8 + x) * 4;
				pixels.set(offset, channel);
				pixels.set(offset + 1, channel);
				pixels.set(offset + 2, bright ? 255 : 70);
				pixels.set(offset + 3, 255);
			}
		}
		return Image.fromRgba8(renderer, 8, 8, pixels);
	}

	static function createShader(renderer:Renderer):Shader {
		var builder = Shader.begin(renderer, ShaderLanguage.Glsl,
			"#version 330\nuniform vec2 offset; layout(location=0) in vec2 position; layout(location=1) in vec3 color0; layout(location=2) in vec2 uv0; out vec3 color; out vec2 uv; void main(){color=color0;uv=uv0;gl_Position=vec4(position+offset,0,1);}",
			"#version 330\nuniform sampler2D tex; in vec3 color; in vec2 uv; out vec4 frag_color; void main(){frag_color=texture(tex,uv)*vec4(color,1);}");
		return builder.uniformBlock(0, ShaderStage.Vertex, 16)
			.uniform(0, 0, "offset", UniformType.Float2)
			.texture(0, 0, ShaderStage.Fragment, "tex")
			.build();
	}

	static function createPipeline(renderer:Renderer, shader:Shader):Pipeline
		return Pipeline.begin(renderer, shader, 28)
			.attribute(0, 0, 0, VertexFormat.Float2)
			.attribute(1, 0, 8, VertexFormat.Float3)
			.attribute(2, 0, 20, VertexFormat.Float2)
			.depthStencil(true)
			.indexType(IndexType.UInt16)
			.build();

	static function applyFrameBindings(pipeline:Pipeline, buffer:Buffer, indexBuffer:Buffer,
		image:Image, sampler:Sampler):Void {
		pipeline.apply();
		buffer.applyVertex(0);
		indexBuffer.applyIndex();
		image.apply(0);
		sampler.apply(0);
	}

	static function drawImmediate(renderer:Renderer):Void {
		for (y in 0...20) {
			for (x in 0...20) {
				renderer.uniforms(16)
					.writeFloat(0, -0.90 + x * 0.095)
					.writeFloat(4, -0.90 + y * 0.095)
					.apply(0);
				renderer.draw(0, 6);
			}
		}
	}

	static function encodeBatched(commandBuffer:CommandBuffer, pipeline:Pipeline, buffer:Buffer,
		indexBuffer:Buffer, image:Image, sampler:Sampler):Void {
		commandBuffer.reset();
		commandBuffer.applyPipeline(pipeline);
		commandBuffer.applyVertexBuffer(0, buffer, 0);
		commandBuffer.applyIndexBuffer(indexBuffer, 0);
		commandBuffer.applyImage(0, image);
		commandBuffer.applySampler(0, sampler);
		for (y in 0...20) {
			for (x in 0...20) {
				commandBuffer.applyUniform2f(0, -0.90 + x * 0.095, -0.90 + y * 0.095);
				commandBuffer.draw(0, 6, 1);
			}
		}
	}

	static function main():Int {
		verifyGeneratedEnumMembers();
		var runtime = NativeKitRuntime.start();
		var windowOptions = new WindowOptions();
		windowOptions.set_width(800);
		windowOptions.set_height(600);
		windowOptions.set_title("Haxeon: GPU over NativeKit");
		windowOptions.set_flags(WindowFlags.Resizable);
		windowOptions.set_owner(NativeKit.WindowHandle.invalid());
		windowOptions.set_kind(WindowKind.Normal);
		var window:NativeKitWindow = runtime.createWindow(windowOptions);
		var surface = Surface.create(window, 800, 600);
		var renderer:Null<Renderer> = null;
		var buffer:Null<Buffer> = null;
		var indexBuffer:Null<Buffer> = null;
		var shader:Null<Shader> = null;
		var pipeline:Null<Pipeline> = null;
		var image:Null<Image> = null;
		var sampler:Null<Sampler> = null;
		var ready = false;
		var running = true;
		var frames = 0;
		var retainedTargetImage:Null<GraphicsImageRef> = null;
		var immediateMs = 0.0;
		var batchedMs = 0.0;
		var commandBuffer:Null<CommandBuffer> = null;
		var surfaceReady = false;
		var eventSubscription = runtime.events.listen(function(value) switch value {
			case WindowClose(source)
				if (source.rawValue() == window.nativeHandle().rawValue()):
				running = false;
			case SurfaceReady(source)
				if (source.rawValue() == surface.nativeHandle().rawValue()):
				surfaceReady = true;
			case _:
		});

		while (running) {
			runtime.events.poll();
			if (!ready && surfaceReady) {
				renderer = surface.createRenderer();
				commandBuffer = renderer.commandBuffer(64);

				buffer = createVertexBuffer(renderer);
				indexBuffer = createIndexBuffer(renderer);
				image = createCheckerboard(renderer);
				sampler = Sampler.create(renderer, Filter.Nearest, Filter.Nearest,
					Wrap.Repeat, Wrap.Repeat);
				shader = createShader(renderer);
				pipeline = createPipeline(renderer, shader);
				var peerRenderer = surface.createRenderer();
				var wrongShaderRendererRejected = false;
				try Pipeline.begin(peerRenderer, shader, 28) catch (_:Dynamic) wrongShaderRendererRejected = true;
				if (!wrongShaderRendererRejected)
					throw "GPU pipeline accepted a shader from another renderer";
				peerRenderer.beginFrame();
				var wrongCommandRendererRejected = false;
				try peerRenderer.submit(commandBuffer) catch (_:Dynamic) wrongCommandRendererRejected = true;
				if (!wrongCommandRendererRejected)
					throw "GPU renderer accepted a command buffer from another renderer";
				peerRenderer.endFrame();
				peerRenderer.dispose();

				var target = RenderTarget.create(renderer, 32, 32, true);
				target.begin();
				var endFrameRejected = false;
				try renderer.endFrame() catch (_:Dynamic) endFrameRejected = true;
				if (!endFrameRejected)
					throw "window endFrame accepted an offscreen target pass";
				var resourceCreationRejected = false;
				try Sampler.create(renderer) catch (_:Dynamic) resourceCreationRejected = true;
				if (!resourceCreationRejected)
					throw "GPU resource creation succeeded during an active pass";
				applyFrameBindings(pipeline, buffer, indexBuffer, image, sampler);
				renderer.uniforms(16).writeFloat(0, 0).writeFloat(4, 0).apply(0);
				renderer.draw(0, 6);
				target.end();
				retainedTargetImage = target.sampledImage();
				if (retainedTargetImage.width != 32 || retainedTargetImage.height != 32
					|| (retainedTargetImage.api != GraphicsApi.Opengl
						&& retainedTargetImage.api != GraphicsApi.OpenglEs
						&& retainedTargetImage.api != GraphicsApi.D3d11
						&& retainedTargetImage.api != GraphicsApi.Metal))
					throw "offscreen target image metadata mismatch";
				target.dispose();
				ready = true;
			}

			if (ready && running) {
				renderer.beginFrame();
				if (frames == 0 && NativeKitGpu.nkgpu_submit_commands(renderer.nativeHandle(), Bytes.alloc(4), 4) != -2)
					throw "truncated command stream was accepted";
				var started = Date.now().getTime();
				if (frames < 12) {
					applyFrameBindings(pipeline, buffer, indexBuffer, image, sampler);
					drawImmediate(renderer);
					immediateMs += Date.now().getTime() - started;
				} else {
					encodeBatched(commandBuffer, pipeline, buffer, indexBuffer, image, sampler);
					renderer.submit(commandBuffer);
					batchedMs += Date.now().getTime() - started;
				}
				renderer.endFrame();
				frames += 1;
				if (frames >= 24)
					running = false;
			}
		}

		var stalePipeline = pipeline.nativeHandle();
		sampler.dispose();
		sampler.dispose();
		image.dispose();
		pipeline.dispose();
		if (NativeKitGpu.nkgpu_pipeline_destroy(renderer.nativeHandle(), stalePipeline) != -3)
			throw "stale pipeline handle was accepted";
		shader.dispose();
		indexBuffer.dispose();
		renderer.dispose();
		if (!buffer.isDisposed())
			throw "GPU renderer did not dispose its remaining resources";
		buffer.dispose();
		buffer.dispose();
		var blockedRuntimeShutdown = false;
		try runtime.dispose() catch (_:Dynamic) blockedRuntimeShutdown = true;
		if (!blockedRuntimeShutdown || runtime.isDisposed())
			throw "runtime shut down with a retained graphics image";
		retainedTargetImage.dispose();
		surface.dispose();
		eventSubscription.dispose();
		runtime.dispose();

		Sys.println("draws_per_frame=400");
		Sys.println("immediate_cpu_ms_per_frame=" + immediateMs / 12.0);
		Sys.println("batched_encode_and_submit_ms_per_frame=" + batchedMs / 12.0);
		return frames == 24 && immediateMs > batchedMs ? 42 : 4;
	}
}
