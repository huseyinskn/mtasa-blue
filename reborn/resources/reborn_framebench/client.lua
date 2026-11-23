local benchmark
local maxSamples = 30000

local function finishBenchmark()
    local result = benchmark
    benchmark = nil
    local count = #result.samples
    if count == 0 or result.elapsed <= 0 then
        return
    end

    table.sort(result.samples)
    local p95 = result.samples[math.ceil(count * 0.95)]
    local p99 = result.samples[math.ceil(count * 0.99)]
    local fps = 1000 * count / result.elapsed
    outputConsole(string.format(
        "[Reborn] %d frames, %.2f s, %.2f FPS, p95 %.1f ms, p99 %.1f ms, %d frames > 33.3 ms",
        count, result.elapsed / 1000, fps, p95, p99, result.slowFrames
    ))
end

addCommandHandler("rebornbench", function(_, seconds)
    local duration = tonumber(seconds or "60")
    if not duration or duration ~= math.floor(duration) or duration < 5 or duration > 120 then
        outputConsole("Usage: rebornbench [5-120 seconds]")
        return
    end

    benchmark = { samples = {}, elapsed = 0, slowFrames = 0, duration = duration * 1000 }
    outputConsole(string.format("[Reborn] Measuring frame intervals for up to %d seconds.", duration))
end)

addEventHandler("onClientPreRender", root, function(frameMs)
    if not benchmark then
        return
    end

    -- The first interval began before the benchmark command.
    if not benchmark.started then
        benchmark.started = true
        return
    end

    local samples = benchmark.samples
    samples[#samples + 1] = frameMs
    benchmark.elapsed = benchmark.elapsed + frameMs
    if frameMs > 1000 / 30 then
        benchmark.slowFrames = benchmark.slowFrames + 1
    end

    if benchmark.elapsed >= benchmark.duration or #samples >= maxSamples then
        finishBenchmark()
    end
end)

addEventHandler("onClientMinimize", root, function()
    if benchmark then
        benchmark = nil
        outputConsole("[Reborn] Benchmark cancelled because the game was minimized.")
    end
end)
