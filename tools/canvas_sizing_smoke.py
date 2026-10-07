"""Shared CDP regression for CSS-owned NativeKit canvases."""
import time


def check_canvas_sizing(page):
    # Resize after NativeKit has assigned inline startup dimensions. Check both
    # CSS bounds and the renderer's backing buffer, including a scale change.
    assert page.evaluate("document.getElementById('canvas').style.width") == ""
    for width, height, scale in ((1920, 1080, 1), (1000, 700, 1), (1000, 700, 2), (1400, 900, 1)):
        page.command("Emulation.setDeviceMetricsOverride", {
            "width": width, "height": height, "deviceScaleFactor": scale, "mobile": False})
        deadline = time.monotonic() + 15
        while True:
            size = page.evaluate("""(() => {
                const canvas = document.getElementById('canvas');
                return [canvas.clientWidth, canvas.clientHeight, canvas.width, canvas.height];
            })()""")
            if size == [width, height, width * scale, height * scale]:
                break
            assert time.monotonic() < deadline, {"expected": [width, height, scale], "actual": size}
            time.sleep(0.1)
    # A parent-only layout change does not fire a window resize event.
    page.evaluate("""(() => {
        const canvas = document.getElementById('canvas');
        const container = document.createElement('div');
        container.id = 'resize-test-container';
        container.style.cssText = 'width: 800px; height: 600px';
        canvas.before(container);
        container.append(canvas);
        canvas.style.width = '100%';
        canvas.style.height = '100%';
    })()""")
    for width, height in ((800, 600), (1100, 750)):
        page.evaluate(f"document.getElementById('resize-test-container').style.cssText = 'width: {width}px; height: {height}px'")
        deadline = time.monotonic() + 15
        while True:
            size = page.evaluate("[document.getElementById('canvas').width, document.getElementById('canvas').height]")
            if size == [width, height]:
                break
            assert time.monotonic() < deadline, {"container": [width, height], "framebuffer": size}
            time.sleep(0.1)
    page.evaluate("""(() => {
        const canvas = document.getElementById('canvas');
        const container = document.getElementById('resize-test-container');
        container.before(canvas);
        container.remove();
        canvas.style.removeProperty('width');
        canvas.style.removeProperty('height');
    })()""")
    page.command("Emulation.clearDeviceMetricsOverride")
    deadline = time.monotonic() + 15
    while True:
        ready = page.evaluate("""(() => {
            const canvas = document.getElementById('canvas');
            const scale = Math.max(1, window.devicePixelRatio);
            return canvas.clientWidth === innerWidth && canvas.clientHeight === innerHeight
                && canvas.width === Math.round(innerWidth * scale)
                && canvas.height === Math.round(innerHeight * scale);
        })()""")
        if ready:
            break
        assert time.monotonic() < deadline, "Canvas did not return to viewport layout"
        time.sleep(0.1)
    print("PASS: canvas follows viewport/container growth, shrinkage and device scale changes")
