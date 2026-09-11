import NativeKit;
import NativeKit.EventKind;
import NativeKit.InitOptions;
import NativeKit.NativeKitConstants;
import NativeKitEvent;
import NativeKitEventValue;
import NativeKitOptions;
import NativeKitSokol;

class Triangle {
	static function checked(status:Int):Void {
		if (status != 0)
			throw "NativeKit-Sokol failed: " + status + ": " + NativeKitSokol.nks_last_error();
	}

	static function createVertexBuffer(renderer:nks_renderer):nks_buffer {
		var values = [
			0.0 - 0.04, 0.04, 1.0, 0.30, 0.35, 0.0, 0.0,
			0.04, 0.04, 0.25, 0.85, 0.55, 2.0, 0.0,
			0.04, 0.0 - 0.04, 0.30, 0.55, 1.0, 2.0, 2.0,
			0.0 - 0.04, 0.0 - 0.04, 1.0, 0.85, 0.25, 0.0, 2.0
		];
		var madeBuilder = NativeKitSokol.nks_buffer_begin(renderer, values.length * 4);
		checked(madeBuilder.status);
		var builder = madeBuilder.out_builder;
		for (index in 0...values.length)
			checked(NativeKitSokol.nks_buffer_write_f32(builder, index * 4, values[index]));
		var madeBuffer = NativeKitSokol.nks_buffer_end(builder);
		checked(madeBuffer.status);
		if (NativeKitSokol.nks_buffer_write_f32(builder, 0, 0.0) != 0 - 3)
			throw "completed buffer builder was not invalidated";
		return madeBuffer.out_buffer;
	}

	static function applyFrameBindings(renderer:nks_renderer, pipeline:nks_pipeline,
		buffer:nks_buffer, indexBuffer:nks_buffer, image:nks_image, sampler:nks_sampler):Void {
		checked(NativeKitSokol.nks_apply_pipeline(renderer, pipeline));
		checked(NativeKitSokol.nks_apply_vertex_buffer(renderer, 0, buffer, 0));
		checked(NativeKitSokol.nks_apply_index_buffer(renderer, indexBuffer, 0));
		checked(NativeKitSokol.nks_apply_image(renderer, 0, image));
		checked(NativeKitSokol.nks_apply_sampler(renderer, 0, sampler));
	}

	static function drawImmediate(renderer:nks_renderer):Void {
		for (y in 0...20) {
			for (x in 0...20) {
				var uniforms = NativeKitSokol.nks_uniforms_begin(renderer, 8);
				checked(uniforms.status);
				checked(NativeKitSokol.nks_uniforms_write_f32(uniforms.out_builder, 0,
					-0.90 + x * 0.095));
				checked(NativeKitSokol.nks_uniforms_write_f32(uniforms.out_builder, 4,
					-0.90 + y * 0.095));
				checked(NativeKitSokol.nks_apply_uniforms(renderer, 0, uniforms.out_builder));
				checked(NativeKitSokol.nks_draw(renderer, 0, 6, 1));
			}
		}
	}

	static function encodeBatched(commandBuffer:SokolCommandBuffer, pipeline:nks_pipeline,
		buffer:nks_buffer, indexBuffer:nks_buffer, image:nks_image, sampler:nks_sampler):Void {
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

	static function createCheckerboard(renderer:nks_renderer):nks_image {
		var madeBuilder = NativeKitSokol.nks_image_begin(renderer, 8, 8);
		checked(madeBuilder.status);
		var builder = madeBuilder.out_builder;
		for (y in 0...8) {
			for (x in 0...8) {
				var bright = ((x + y) & 1) == 0;
				var channel = bright ? 240 : 35;
				checked(NativeKitSokol.nks_image_write_rgba8(builder, x, y, channel, channel,
					bright ? 255 : 70, 255));
			}
		}
		var madeImage = NativeKitSokol.nks_image_end(builder);
		checked(madeImage.status);
		return madeImage.out_image;
	}

	static function createIndexBuffer(renderer:nks_renderer):nks_buffer {
		var indices = [0, 1, 2, 0, 2, 3];
		var madeBuilder = NativeKitSokol.nks_buffer_begin_kind(renderer, indices.length * 2, 2);
		checked(madeBuilder.status);
		var builder = madeBuilder.out_builder;
		for (index in 0...indices.length)
			checked(NativeKitSokol.nks_buffer_write_u16(builder, index * 2, indices[index]));
		var madeBuffer = NativeKitSokol.nks_buffer_end(builder);
		checked(madeBuffer.status);
		return madeBuffer.out_buffer;
	}

	static function main():Int {
		var init = new InitOptions();
		init.set_struct_size(InitOptions.size());
		init.set_api_version(NativeKitConstants.NK_API_VERSION);
		if (NativeKit.nk_init(init) != 0)
			return 1;

		var windowOptions = NativeKitOptions.window(800, 600, "Haxeon: Sokol over NativeKit");
		var createdWindow = NativeKit.nk_window_create(windowOptions);
		if (createdWindow.status != 0)
			return 2;
		var window = createdWindow.out_window;
		var createdSurface = NativeKitSokol.nks_surface_create(window, 800, 600);
		if (createdSurface.status != 0)
			return 3;
		var surface = createdSurface.out_surface;

		var renderer = new nks_renderer();
		var buffer = new nks_buffer();
		var indexBuffer = new nks_buffer();
		var shader = new nks_shader();
		var pipeline = new nks_pipeline();
		var image = new nks_image();
		var sampler = new nks_sampler();
		var running = true;
		var ready = false;
		var frames = 0;
		var immediateMs = 0.0;
		var batchedMs = 0.0;
		var commandBuffer = new SokolCommandBuffer(64);
		while (running) {
			var event = NativeKitEvent.poll();
			var eventKind = event.kind;
			var eventSource = event.source;
			event.release();
			if (eventKind == EventKind.WindowClose && eventSource == window)
				running = false;
			if (eventKind == EventKind.SurfaceReady && eventSource == surface) {
					var madeRenderer = NativeKitSokol.nks_renderer_create(surface);
					checked(madeRenderer.status);
					renderer = madeRenderer.out_renderer;

					buffer = createVertexBuffer(renderer);
					indexBuffer = createIndexBuffer(renderer);
					image = createCheckerboard(renderer);
					var madeSampler = NativeKitSokol.nks_sampler_create(renderer, 1, 1, 1, 1);
					checked(madeSampler.status);
					sampler = madeSampler.out_sampler;

					var madeShaderBuilder = NativeKitSokol.nks_shader_begin(renderer,
						"#version 330\nuniform vec2 offset; layout(location=0) in vec2 position; layout(location=1) in vec3 color0; layout(location=2) in vec2 uv0; out vec3 color; out vec2 uv; void main(){color=color0;uv=uv0;gl_Position=vec4(position+offset,0,1);}",
						"#version 330\nuniform sampler2D tex; in vec3 color; in vec2 uv; out vec4 frag_color; void main(){frag_color=texture(tex,uv)*vec4(color,1);}");
					checked(madeShaderBuilder.status);
					var shaderBuilder = madeShaderBuilder.out_builder;
					checked(NativeKitSokol.nks_shader_uniform_block(shaderBuilder, 0, 1, 8));
					checked(NativeKitSokol.nks_shader_uniform(shaderBuilder, 0, 0, "offset", 2, 1));
					checked(NativeKitSokol.nks_shader_texture(shaderBuilder, 0, 0, 2, "tex"));
					var madeShader = NativeKitSokol.nks_shader_end(shaderBuilder);
					checked(madeShader.status); shader = madeShader.out_shader;

					var madeBuilder = NativeKitSokol.nks_pipeline_begin(renderer, shader, 28);
					checked(madeBuilder.status);
					var builder = madeBuilder.out_builder;
					checked(NativeKitSokol.nks_pipeline_attribute(builder, 0, 0, 0, 2));
					checked(NativeKitSokol.nks_pipeline_attribute(builder, 1, 0, 8, 3));
					checked(NativeKitSokol.nks_pipeline_attribute(builder, 2, 0, 20, 2));
					checked(NativeKitSokol.nks_pipeline_index_type(builder, 1));
					var madePipeline = NativeKitSokol.nks_pipeline_end(builder);
					checked(madePipeline.status);
					if (NativeKitSokol.nks_pipeline_attribute(builder, 0, 0, 0, 2) != 0 - 3)
						throw "completed pipeline builder was not invalidated";
					pipeline = madePipeline.out_pipeline;
					ready = true;
			}

			if (ready && running) {
				checked(NativeKitSokol.nks_begin_frame(renderer));
				if (frames == 0 && NativeKitSokol.nks_submit_commands(renderer,
					haxe.io.Bytes.alloc(4), 4) != 0 - 2)
					throw "truncated command stream was accepted";
				var started = Date.now().getTime();
				if (frames < 12) {
					applyFrameBindings(renderer, pipeline, buffer, indexBuffer, image, sampler);
					drawImmediate(renderer);
					immediateMs += Date.now().getTime() - started;
				} else {
					encodeBatched(commandBuffer, pipeline, buffer, indexBuffer, image, sampler);
					checked(NativeKitSokol.nks_submit_commands(renderer, commandBuffer.data(),
						commandBuffer.size()));
					batchedMs += Date.now().getTime() - started;
				}
				checked(NativeKitSokol.nks_end_frame(renderer));
				frames += 1;
				if (frames >= 24) running = false;
			}
		}

		if (ready) {
			checked(NativeKitSokol.nks_sampler_destroy(renderer, sampler));
			checked(NativeKitSokol.nks_image_destroy(renderer, image));
			checked(NativeKitSokol.nks_pipeline_destroy(renderer, pipeline));
			if (NativeKitSokol.nks_pipeline_destroy(renderer, pipeline) != 0 - 3)
				throw "stale pipeline handle was accepted";
			checked(NativeKitSokol.nks_shader_destroy(renderer, shader));
			checked(NativeKitSokol.nks_buffer_destroy(renderer, indexBuffer));
			checked(NativeKitSokol.nks_buffer_destroy(renderer, buffer));
			checked(NativeKitSokol.nks_renderer_destroy(renderer));
		}
		checked(NativeKitSokol.nks_surface_destroy(surface));
		NativeKit.nk_window_destroy(window);
		NativeKit.nk_shutdown();
		Sys.println("draws_per_frame=400");
		Sys.println("immediate_cpu_ms_per_frame=" + immediateMs / 12.0);
		Sys.println("batched_encode_and_submit_ms_per_frame=" + batchedMs / 12.0);
		return frames == 24 && immediateMs > batchedMs ? 42 : 4;
	}
}
