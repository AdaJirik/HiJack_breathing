// Run from the repository root: node tools/check_dashboard.cjs
// Exercises the actual embedded script with simulated sensor snapshots.
const fs = require('fs'), vm = require('vm'), assert = require('assert');
const source = fs.readFileSync('firmware/include/web_ui.h', 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const ctx = new Proxy({}, { get: (obj, key) => obj[key] || (() => {}), set: (obj, key, value) => (obj[key] = value, true) });
function element() {
  const children = new Map();
  return { style: {}, append() {}, setAttribute() {}, textContent: '', disabled: false, clicks:0,click(){this.clicks++;},
    querySelector(selector) { if (!children.has(selector)) children.set(selector, element()); return children.get(selector); },
    getBoundingClientRect: () => ({ width: 800, height: 500 }), getContext: () => ctx };
}
let clock = 100;
const sandbox = {
  document: { getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); }, createElement: element },
  performance: { now: () => clock }, devicePixelRatio: 1, requestAnimationFrame() {}, setTimeout() {},
  AbortSignal, URLSearchParams, fetch: () => new Promise(() => {}), console
};
vm.createContext(sandbox); vm.runInContext(source, sandbox);
const run = code => vm.runInContext(code, sandbox);
const sample = { ok: true, age: 0, q: [1, 0, 0, 0], a: [0, 0, 9.80665], gyro: [0, 0, 0], linear: [0, 0, 0], calibrated: false, calibration: 0, progress: 0, seq: 1, errors: 0 };
let sequence=0;
function send(patch) { sandbox.sample = { ...sample, seq:++sequence, ...patch }; run('receive(sample)'); }
send({}); assert.equal(elements.get('calibrate').disabled, false); assert.match(elements.get('mode').textContent, /calibration recommended/);
assert.equal(run('history.length'), 1, 'Raw chart must collect samples before calibration');
assert.equal(run('history[0].v[2]'), 9.80665);
assert.equal(elements.get('traceLinear').disabled, true);
run('drawChart()');
send({ calibration: 1, progress: 50 }); assert.equal(elements.get('calibrate').disabled, true); assert.match(elements.get('message').textContent, /50%/);
send({ calibration: -1 }); assert.match(elements.get('message').textContent, /noise allowance/);
send({ calibration: -2 }); assert.match(elements.get('message').textContent, /horizontally/);
send({ calibration: -3 }); assert.match(elements.get('message').textContent, /outside the calibration range/);
send({ calibrated: true, calibration: 2, linear: [-1, 0, 0] }); assert.match(elements.get('direction').textContent, /−X/); assert.equal(elements.get('intensity').textContent, '1.00');
assert.equal(elements.get('traceLinear').disabled, false);
run("setTraceMode('linear'); drawScene(); drawChart()");
assert.equal(run('traceSamples(performance.now()).length'), 1, 'Linear trace must exclude uncalibrated measurements');
assert.ok(run('traceRange(traceSamples(performance.now()),0)[0] < -1'));
run("setTraceMode('raw')");
assert.ok(run('traceSamples(performance.now()).length') > 1);
assert.ok(run('traceRange(traceSamples(performance.now()),2)[0] > 9'), 'Raw view must zoom to the measured gravity baseline');
send({ ok: false }); assert.equal(elements.get('calibrate').disabled, true); assert.match(elements.get('connection').textContent, /unavailable/);
send({ calibrated: true, calibration: 2 }); clock += 1500; run('render()'); assert.equal(elements.get('calibrate').disabled, true); assert.match(elements.get('connection').textContent, /paused/);
clock+=8001; assert.equal(run('traceSamples(performance.now()).length'),0, 'Old measurements must leave the visible time window');
const rotated = run('rotate([1,0,0],[Math.SQRT1_2,0,0,Math.SQRT1_2])');
assert.ok(Math.abs(rotated[0]) < 1e-6 && Math.abs(rotated[1] - 1) < 1e-6);
assert.ok(run('project([0,0,1],800,500)[1] < project([0,0,0],800,500)[1]'), 'Positive Z should point up on screen');
console.log('Dashboard: raw trace before calibration, linear toggle, per-axis scaling, calibration messages, rendering, sensor faults and stale data checks passed.');

(async()=>{
  const record={phase:1,count:500,elapsed_ms:5000,id:'test-123',label:'calm_breathing',filename:'custom_calm_breathing.csv'};
  send({recording:record});
  assert.equal(elements.get('recordStart').disabled,true);
  assert.equal(elements.get('calibrate').disabled,true);
  assert.equal(elements.get('recordFill').style.width,'50%');
  assert.match(elements.get('recordStatus').textContent,/5.0 seconds left/);
  assert.equal(elements.get('recordReview').hidden,true);
  send({recording:{...record,phase:2,count:1000,elapsed_ms:10000}});
  assert.equal(elements.get('recordReview').hidden,false);
  assert.equal(elements.get('recordConfirm').disabled,false);
  assert.equal(elements.get('recordDownload').clicks,0,'Completion must not download without confirmation');
  assert.match(elements.get('recordSummary').textContent,/custom_calm_breathing/);
  let calls=[];
  sandbox.fetch=async(url,options)=>{calls.push({url,body:String(options.body)});return{ok:true,json:async()=>({...record,phase:3,count:1000,elapsed_ms:10000})};};
  await run("recordingAction('confirm')");
  assert.match(calls[0].url,/confirm$/);assert.equal(calls[0].body,'id=test-123');
  assert.equal(elements.get('recordDownload').clicks,1);
  assert.equal(elements.get('recordDownload').download,record.filename);
  assert.equal(elements.get('recordReview').hidden,true);
  send({recording:{...record,phase:4,error:'Sensor disconnected'}});
  assert.equal(elements.get('recordConfirm').hidden,true);
  assert.equal(elements.get('recordStart').disabled,true);
  assert.match(elements.get('recordStatus').textContent,/Sensor disconnected/);
  await run("recordingAction('discard')");assert.match(calls.at(-1).url,/discard$/);
  assert.equal(run('recording.phase'),0);
  elements.get('recordType').value='fast_breathing';elements.get('recordFilename').value='My custom name';
  await run("recordingAction('start')");assert.match(calls.at(-1).body,/label=fast_breathing/);assert.match(calls.at(-1).body,/filename=My\+custom\+name/);
  sandbox.fetch=async()=>({ok:false,json:async()=>({error:'Confirm or discard first'})});
  await run("recordingAction('start')");assert.match(elements.get('recordStatus').textContent,/Confirm or discard first/);
  console.log('Recorder UI: countdown, confirmation before download, filename/label, interrupted capture, discard and server-error checks passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
