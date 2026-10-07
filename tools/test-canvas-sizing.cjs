#!/usr/bin/env node
// Exercise the shipped EM_JS canvas bridge without compiling the native host.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../src/web/host.cpp'), 'utf8');
const canvases = new Map();
const notifications = [], observers = [], queries = [];
const context = vm.createContext({
  UTF8ToString: value => value,
  document: {
    querySelector: selector => canvases.get(selector),
    createElement: () => ({tagName: 'CANVAS', dataset: {}, style: {}, setAttribute() {}, getAttribute() {}}),
    body: {appendChild: canvas => canvases.set('#' + canvas.id, canvas)}
  },
  ccall: (...args) => notifications.push(args),
  ResizeObserver: class {
    constructor(callback) { this.callback = callback; observers.push(this); }
    observe(canvas) { this.canvas = canvas; }
    disconnect() { this.disconnected = true; }
  },
  window: {devicePixelRatio: 1, matchMedia: query => {
    const media = {query, addEventListener: (_, cb) => { media.callback = cb; },
      removeEventListener: (_, cb) => { assert.equal(media.callback, cb); media.removed = true; }};
    queries.push(media);
    return media;
  }}
});
function bridge(name, args) {
  const pattern = new RegExp('EM_JS\\([^,]+, ' + name + ', \\([^)]*\\), \\{([\\s\\S]*?)\\n\\}\\);');
  const match = source.match(pattern);
  assert.ok(match, name);
  return vm.runInContext('(function(' + args.join(',') + '){' + match[1] + '\n})', context);
}
const create = bridge('nk_web_create_canvas', ['selector', 'width', 'height', 'owned']);
const size = bridge('nk_web_set_canvas_css_size', ['selector', 'width', 'height']);
const observe = bridge('nk_web_observe_canvas_size', ['selector', 'route']);
const resizable = bridge('nk_web_set_canvas_resizable', ['selector', 'enabled']);
const embedded = context.document.createElement();
embedded.style.width = '100%'; embedded.style.height = '100vh';
canvases.set('#canvas', embedded);
assert.equal(create('#canvas', 800, 600, 0), 1);
size('#canvas', 400, 300); // Window and surface sizing must preserve page CSS.
assert.equal(embedded.style.width, '100%');
assert.equal(embedded.style.height, '100vh');
assert.equal(embedded.dataset.nativekitSizing, 'css');
embedded.dataset.nativekitSizing = 'native';
create('#canvas', 800, 600, 0);
size('#canvas', 400, 300);
assert.equal(embedded.style.width, '400px');
assert.equal(embedded.style.height, '300px');
create('#auto-primary', 320, 240, 0);
assert.equal(canvases.get('#auto-primary').dataset.nativekitSizing, 'native');
create('#nativekit-window-2', 640, 480, 1);
assert.equal(canvases.get('#nativekit-window-2').style.width, '640px');
assert.equal(canvases.get('#nativekit-window-2').dataset.nativekitSizing, 'native');
observe('#canvas', 7);
resizable('#canvas', 0, 7);
assert.ok(!observers[0].disconnected, 'CSS layout must be observed even when user resizing is disabled');
observers[0].callback();
assert.equal(notifications.at(-1)[3][0], 7);
context.window.devicePixelRatio = 2;
queries[0].callback();
assert.ok(queries[0].removed);
assert.equal(queries[1].query, '(resolution: 2dppx)');
assert.equal(notifications.length, 2);
observe('#canvas', 0);
assert.ok(observers[0].disconnected);
assert.ok(queries[1].removed);
assert.equal(embedded._nkStopSizeObserver, undefined);
console.log('PASS: CSS/native canvas sizing, non-resizable layout observation, DPR changes and cleanup');
