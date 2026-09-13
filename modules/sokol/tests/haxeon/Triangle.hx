import NativeKit.EventKind;
import NativeKit.GraphicsApi;
import NativeKitEvent;
import NativeKitOptions;
import NativeKitRuntime;
import NativeKitWindow;
import NativeKitSokol;
import GraphicsImageRef;
import SokolEnums.SokolFilter;
import SokolEnums.SokolIndexType;
import SokolEnums.SokolShaderStage;
import SokolEnums.SokolUniformType;
import SokolEnums.SokolVertexFormat;
import SokolEnums.SokolWrap;
import haxe.io.Bytes;

class Triangle {
	static function createVertexBuffer(renderer:SokolRenderer):SokolBuffer {
		var values = [
			-0.04, 0.04, 1.0, 0.30, 0.35, 0.0, 0.0,
			0.04, 0.04, 0.25, 0.85, 0.55, 2.0, 0.0,
			0.04, -0.04, 0.30, 0.55, 1.0, 2.0, 2.0,
			-0.04, -0.04, 1.0, 0.85, 0.25, 0.0, 2.0
		];
		return SokolBuffer.vertexFloats(renderer, values);
	}

	static function createIndexBuffer(renderer:SokolRenderer):SokolBuffer
		return SokolBuffer.indices16(renderer, [0, 1, 2, 0, 2, 3]);

	static function createCheckerboard(renderer:SokolRenderer):SokolImage {
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
		return SokolImage.fromRgba8(renderer, 8, 8, pixels);
	}

	static function createShader(renderer:SokolRenderer):SokolShader {
		var builder = SokolShader.begin(renderer,
			"#version 330\nuniform vec2 offset; layout(location=0) in vec2 position; layout(location=1) in vec3 color0; layout(location=2) in vec2 uv0; out vec3 color; out vec2 uv; void main(){color=color0;uv=uv0;gl_Position=vec4(position+offset,0,1);}",
			"#version 330\nuniform sampler2D tex; in vec3 color; in vec2 uv; out vec4 frag_color; void main(){frag_color=texture(tex,uv)*vec4(color,1);}");
		return builder.uniformBlock(0, SokolShaderStage.Vertex, 8)
			.uniform(0, 0, "offset", SokolUniformType.Float2)
			.texture(0, 0, SokolShaderStage.Fragment, "tex")
			.build();
	}

	static function createPipeline(renderer:SokolRenderer, shader:SokolShader):SokolPipeline
		return SokolPipeline.begin(renderer, shader, 28)
			.attribute(0, 0, 0, SokolVertexFormat.Float2)
			.attribute(1, 0, 8, SokolVertexFormat.Float3)
			.attribute(2, 0, 20, SokolVertexFormat.Float2)
			.depthStencil(false)
			.indexType(SokolIndexType.UInt16)
			.build();

	static function applyFrameBindings(pipeline:SokolPipeline, buffer:SokolBuffer, indexBuffer:SokolBuffer,
		image:SokolImage, sampler:SokolSampler):Void {
		pipeline.apply();
		buffer.applyVertex(0);
		indexBuffer.applyIndex();
		image.apply(0);
		sampler.apply(0);
	}

	static function drawImmediate(renderer:SokolRenderer):Void {
		for (y in 0...20) {
			for (x in 0...20) {
				renderer.uniforms(8)
					.writeFloat(0, -0.90 + x * 0.095)
					.writeFloat(4, -0.90 + y * 0.095)
					.apply(0);
				renderer.draw(0, 6);
			}
		}
	}

	static function encodeBatched(commandBuffer:SokolCommandBuffer, pipeline:SokolPipeline, buffer:SokolBuffer,
		indexBuffer:SokolBuffer, image:SokolImage, sampler:SokolSampler):Void {
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
		var runtime = NativeKitRuntime.start(NativeKitOptions.init());
		var window:NativeKitWindow = runtime.createWindow(NativeKitOptions.window(800, 600,
			"Haxeon: Sokol over NativeKit"));
		var surface = SokolSurface.create(window, 800, 600);
		var renderer:Null<SokolRenderer> = null;
		var buffer:Null<SokolBuffer> = null;
		var indexBuffer:Null<SokolBuffer> = null;
		var shader:Null<SokolShader> = null;
		var pipeline:Null<SokolPipeline> = null;
		var image:Null<SokolImage> = null;
		var sampler:Null<SokolSampler> = null;
		var ready = false;
		var running = true;
		var frames = 0;
		var retainedTargetImage:Null<GraphicsImageRef> = null;
		var immediateMs = 0.0;
		var batchedMs = 0.0;
		var commandBuffer:Null<SokolCommandBuffer> = null;

		while (running) {
			var event = NativeKitEvent.poll();
			var eventKind = event.kind;
			var eventSource = event.source;
			event.release();
			if (eventKind == EventKind.WindowClose && eventSource.rawValue() == window.nativeHandle().rawValue())
				running = false;
			if (!ready && eventKind == EventKind.SurfaceReady
				&& eventSource.rawValue() == surface.nativeHandle().rawValue()) {
				renderer = surface.createRenderer();
				commandBuffer = renderer.commandBuffer(64);

				var target = SokolRenderTarget.create(renderer, 32, 32);
				target.begin();
				target.end();
				retainedTargetImage = target.sampledImage();
				if (retainedTargetImage.width != 32 || retainedTargetImage.height != 32
					|| (retainedTargetImage.api != GraphicsApi.Opengl && retainedTargetImage.api != GraphicsApi.OpenglEs))
					throw "offscreen target image metadata mismatch";
				target.dispose();

				buffer = createVertexBuffer(renderer);
				indexBuffer = createIndexBuffer(renderer);
				image = createCheckerboard(renderer);
				sampler = SokolSampler.create(renderer, SokolFilter.Nearest, SokolFilter.Nearest,
					SokolWrap.Repeat, SokolWrap.Repeat);
				shader = createShader(renderer);
				pipeline = createPipeline(renderer, shader);
				ready = true;
			}

			if (ready && running) {
				renderer.beginFrame();
				if (frames == 0 && NativeKitSokol.nks_submit_commands(renderer.nativeHandle(), Bytes.alloc(4), 4) != -2)
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
		image.dispose();
		pipeline.dispose();
		if (NativeKitSokol.nks_pipeline_destroy(renderer.nativeHandle(), stalePipeline) != -3)
			throw "stale pipeline handle was accepted";
		shader.dispose();
		indexBuffer.dispose();
		buffer.dispose();
		renderer.dispose();
		var blockedRuntimeShutdown = false;
		try runtime.dispose() catch (_:Dynamic) blockedRuntimeShutdown = true;
		if (!blockedRuntimeShutdown || runtime.isDisposed())
			throw "runtime shut down with a retained graphics image";
		retainedTargetImage.dispose();
		surface.dispose();
		runtime.dispose();

		Sys.println("draws_per_frame=400");
		Sys.println("immediate_cpu_ms_per_frame=" + immediateMs / 12.0);
		Sys.println("batched_encode_and_submit_ms_per_frame=" + batchedMs / 12.0);
		return frames == 24 && immediateMs > batchedMs ? 42 : 4;
	}
}
