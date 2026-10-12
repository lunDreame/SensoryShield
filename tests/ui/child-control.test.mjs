import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import ts from '../../frontend/node_modules/typescript/lib/typescript.js';
import { Readable } from 'node:stream';
import { mockApi } from '../../frontend/dev/mock-api.mjs';

// Execute the actual shared request handler, using the same mock REST API as the browser.
const compiled = ts.transpileModule(readFileSync('frontend/src/shared/lib/child-control.ts', 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 }
}).outputText;
const exported = {};
runInNewContext(compiled, { exports: exported });
const { applyChildComfort } = exported;
let middleware;
mockApi().configureServer({ middlewares: { use(fn) { middleware = fn; } } });
const commands = [];
async function request(url, body) {
  const req = Readable.from(body ? [JSON.stringify(body)] : []);
  req.url = url; req.method = body ? 'POST' : 'GET';
  let response;
  await middleware(req, {
    statusCode: 200, setHeader() {},
    end(raw) { response = { status: this.statusCode, body: JSON.parse(raw) }; }
  }, () => { throw Error('unexpected route'); });
  if (response.status !== 200) throw Error(`HTTP ${response.status}`);
  return response.body;
}
const api = {
  getStatus: () => request('/api/status'),
  getProfile: () => request('/api/profile'),
  setLight: body => { commands.push(['light', JSON.parse(JSON.stringify(body))]); return request('/api/light', body); },
  setFan: body => { commands.push(['fan', JSON.parse(JSON.stringify(body))]); return request('/api/fan', body); },
  setMode: mode => { commands.push(['mode', mode]); return request('/api/mode', { mode, durationMinutes: 15 }); }
};
const stored = await api.getProfile();
await request('/api/profile', { ...stored, preferredBrightness: 60, preferredFanPercent: 40, preferredCCTMireds: 250 });
await applyChildComfort('BRIGHT', api);
let state = await api.getStatus();
assert.equal(state.outputs.brightnessPercent, 50);
assert.equal(state.outputs.fanPercent, 40); // Unrelated channel stays untouched.
assert.equal(state.outputs.cctMireds, 250);
assert.equal(state.system.mode, 'OVERRIDE');
assert.ok(state.system.overrideRemainingSeconds > 0 && state.system.overrideRemainingSeconds <= 900);
await applyChildComfort('BRIGHT', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 40); // Latest output, not stale prop state.
await applyChildComfort('DARK', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 50);
await applyChildComfort('LOUD', api);
state = await api.getStatus();
assert.equal(state.outputs.brightnessPercent, 40);
assert.equal(state.outputs.fanPercent, 20);
await applyChildComfort('QUIET', api);
state = await api.getStatus();
assert.equal(state.outputs.fanPercent, 40);
assert.equal(state.outputs.brightnessPercent, 40);

await request('/api/light', { power: false, brightness: 0, cct: 250 });
await request('/api/fan', { power: false, speed: 0 });
commands.length = 0;
await applyChildComfort('BRIGHT', api);
assert.equal(commands.length, 0); // A complaint about brightness must not turn on an off light.
await applyChildComfort('LOUD', api);
assert.equal(commands.length, 0);
await applyChildComfort('QUIET', api);
assert.equal((await api.getStatus()).outputs.fanPercent, 20); // Starts a stopped fan.
await applyChildComfort('LOUD', api);
assert.equal((await api.getStatus()).outputs.fanOn, false); // Stop below minimum motor duty.
assert.equal((await api.getStatus()).outputs.lightOn, false);
await applyChildComfort('DARK', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, stored.minBrightness);

await request('/api/profile', { ...stored, minBrightness: 20, maxBrightness: 50, preferredBrightness: 40, fanMaxPercent: 30, preferredFanPercent: 20 });
await request('/api/light', { power: true, brightness: 25, cct: 333 });
await request('/api/fan', { power: true, speed: 20 });
await applyChildComfort('BRIGHT', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 20);
await applyChildComfort('BRIGHT', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 20);
for (let i = 0; i < 5; i++) await applyChildComfort('DARK', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 50);
for (let i = 0; i < 3; i++) await applyChildComfort('QUIET', api);
assert.equal((await api.getStatus()).outputs.fanPercent, 30);
await request('/api/profile', { fanMaxPercent: 0, preferredFanPercent: 0 });
await request('/api/fan', { power: false, speed: 0 });
commands.length = 0;
assert.match(await applyChildComfort('QUIET', api), /팬을 꺼두도록/);
assert.equal(commands.length, 0);
await request('/api/profile', { fanMaxPercent: 10, preferredFanPercent: 10 });
await request('/api/fan', { power: false, speed: 0 });
commands.length = 0;
assert.match(await applyChildComfort('QUIET', api), /팬을 꺼두도록/);
assert.equal(commands.length, 0); // A cap below the motor minimum never becomes an 18% request.
// Previously lower manual output must never increase on a too-bright request.
await request('/api/light', { power: true, brightness: 10, cct: 333 });
await applyChildComfort('BRIGHT', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 10);
// Previously higher manual output must never decrease on a too-dark request.
await request('/api/light', { power: true, brightness: 70, cct: 333 });
await applyChildComfort('DARK', api);
assert.equal((await api.getStatus()).outputs.brightnessPercent, 70);

await request('/__mock/scenario', { scenario: 'command-error' });
await assert.rejects(() => applyChildComfort('BRIGHT', api), /기기에 알려주지 못했어요/);
await request('/__mock/scenario', { scenario: 'offline' });
commands.length = 0;
await assert.rejects(() => applyChildComfort('DARK', api), /기기에 알려주지 못했어요/);
assert.equal(commands.length, 0);
await request('/__mock/scenario', { scenario: 'normal' });
await request('/api/profile', { ...stored });
await assert.rejects(() => applyChildComfort('LOUD', { ...api, setFan: async () => ({ ok: false }) }), /일부만 바뀌었어요/);
assert.equal((await api.getStatus()).system.mode, 'OVERRIDE'); // Partial changes still get a bounded return time.
await assert.rejects(() => applyChildComfort('LOUD', { ...api, setFan: async () => ({ ok: false }), setMode: async () => ({ ok: false }) }), /일부만 바뀌었고 자동으로 돌아가는 설정도/);
await assert.rejects(() => applyChildComfort('DARK', { ...api, setMode: async () => ({ ok: false }) }), /자동으로 돌아가는 설정을 완료하지 못했어요/);
const safe = await api.getStatus(); safe.system.mode = 'SAFE';
commands.length = 0;
await assert.rejects(() => applyChildComfort('DARK', { ...api, getStatus: async () => safe }), /기기에 알려주지 못했어요/);
assert.equal(commands.length, 0);
console.log('Four child requests, actual REST outputs, preference/range limits, return mode, no-ops and failures passed');
