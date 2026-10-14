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
const api = { requestComfort: action => { commands.push(action); return request('/api/comfort', {action}); } };
let now=Date.now();const realNow=Date.now;Date.now=()=>now;
const state=()=>request('/api/status');
const advance=async ms=>{for(let i=0;i<ms;i+=250){now+=250;await state();}};
const stored=await request('/api/profile');
assert.match(await applyChildComfort('BRIGHT',api),/서서히/);
assert.equal((await state()).outputs.brightnessPercent,100);
await advance(2500);assert.equal((await state()).outputs.brightnessPercent,65);
await advance(2500);assert.equal((await state()).outputs.brightnessPercent,30);
assert.equal((await state()).system.overrideRemainingSeconds,5);
await advance(4500);assert.equal((await state()).system.mode,'OVERRIDE');
await advance(500);assert.equal((await state()).system.mode,'AUTO');
await advance(5000);assert.equal((await state()).outputs.brightnessPercent,100);
await applyChildComfort('LOUD',api);await advance(5000);
assert.equal((await state()).outputs.brightnessPercent,30);assert.equal((await state()).outputs.fanPercent,20);
await request('/api/light',{power:false,brightness:0,cct:250});await request('/api/fan',{power:false,speed:0});
assert.match(await applyChildComfort('BRIGHT',api),/더 바꿀 수 없어요/);
assert.equal((await state()).outputs.lightOn,false);
await applyChildComfort('DARK',api);await advance(5000);assert.equal((await state()).outputs.brightnessPercent,30);
await applyChildComfort('QUIET',api);await advance(5000);assert.equal((await state()).outputs.fanPercent,30);
await request('/api/mode',{mode:'MANUAL'});
await request('/api/profile',{...stored,maxBrightness:50,preferredBrightness:40,fanMaxPercent:10,preferredFanPercent:0});
await request('/api/fan',{power:false,speed:0});
assert.match(await applyChildComfort('QUIET',api),/더 바꿀 수 없어요/);
await request('/api/mode',{mode:'SAFE'});
await assert.rejects(()=>applyChildComfort('DARK',api),/알려주지 못했어요/);
await request('/__mock/scenario',{scenario:'command-error'});
await assert.rejects(()=>applyChildComfort('LOUD',api),/알려주지 못했어요/);
assert.ok(commands.includes('BRIGHT') && commands.includes('DARK') && commands.includes('LOUD') && commands.includes('QUIET'));
Date.now=realNow;
console.log('Child UI: atomic four-action API, 5s arrival/hold/recovery, countdown, bounds, off/no-op and error responses passed');
