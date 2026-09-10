import NativeKitSokolBenchmark;

class Benchmark {
	static function elapsed(start:Float):Float
		return Date.now().getTime() - start;

	static function main():Int {
		var warmup = 0;
		for (index in 0...10000)
			warmup = NativeKitSokolBenchmark.nks_benchmark_call(warmup);

		var iterations = 1000000;
		var direct = 0;
		var directStart = Date.now().getTime();
		for (index in 0...iterations)
			direct = NativeKitSokolBenchmark.nks_benchmark_call(direct);
		var directMs = elapsed(directStart);

		var batchStart = Date.now().getTime();
		var batched = NativeKitSokolBenchmark.nks_benchmark_batch(iterations, 0);
		var batchMs = elapsed(batchStart);

		Sys.println("iterations=" + iterations);
		Sys.println("direct_ms=" + directMs);
		Sys.println("direct_ns_per_call=" + (directMs * 1000000.0 / iterations));
		Sys.println("batched_native_loop_ms=" + batchMs);
		Sys.println("result_match=" + (direct == batched));

		var draws = 10000;
		var commands = haxe.io.Bytes.alloc(draws * 20);
		for (index in 0...draws) {
			var at = index * 20;
			commands.setInt32(at, 7);
			commands.setInt32(at + 4, 20);
			commands.setInt32(at + 8, index);
			commands.setInt32(at + 12, 6);
			commands.setInt32(at + 16, 1);
		}
		var commandDirect = 0;
		var commandDirectStart = Date.now().getTime();
		for (index in 0...draws)
			commandDirect = NativeKitSokolBenchmark.nks_benchmark_call(commandDirect ^ index ^ 6 ^ 1);
		var commandDirectMs = elapsed(commandDirectStart);
		var commandBatchStart = Date.now().getTime();
		var commandBatch = NativeKitSokolBenchmark.nks_benchmark_commands(commands,
			commands.length, 0);
		var commandBatchMs = elapsed(commandBatchStart);
		Sys.println("draw_commands=" + draws);
		Sys.println("per_draw_ffi_ms=" + commandDirectMs);
		Sys.println("packed_command_submit_ms=" + commandBatchMs);
		Sys.println("command_result_match=" + (commandDirect == commandBatch));
		return direct == batched && commandDirect == commandBatch ? 42 : 1;
	}
}
