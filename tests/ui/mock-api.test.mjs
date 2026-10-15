import assert from 'node:assert/strict';
import { mockApi } from '../../frontend/dev/mock-api.mjs';
import { Readable } from 'node:stream';
let middleware;
let now = Date.now();
const realNow = Date.now;
Date.now = () => now;
mockApi().configureServer({middlewares:{use(fn){middleware=fn;}}});
async function request(url, body) {
  const req=Readable.from(body ? [JSON.stringify(body)] : []);
  req.url=url;req.method=body ? 'POST' : 'GET';
  let result;
  const res={statusCode:200,setHeader(){},end(raw){result={status:this.statusCode,body:JSON.parse(raw)};}};
  await middleware(req,res,()=>{throw Error('unexpected route');});
  return result;
}
const profile=(await request('/api/profile')).body;
assert.equal(profile.preferredCCTMireds,454);
assert.equal(profile.preferredBrightness,100); assert.equal(profile.minBrightness,30); assert.equal(profile.maxBrightness,100);
assert.equal(profile.reliefHoldMs,5000); assert.equal(profile.recoverConfirmMs,3000);
assert.equal(profile.occupancyAutomationEnabled,false);
assert.equal((await request('/api/profile',{reliefHoldMs:1800000})).status,200);
assert.equal((await request('/api/profile')).body.reliefHoldMs,1800000);
assert.equal((await request('/api/profile',{reliefHoldMs:3600001})).status,400);
await request('/api/profile',profile);
assert.equal((await request('/api/profile',{occupancyTimeoutMs:900000})).status,200);
assert.equal((await request('/api/profile')).body.occupancyTimeoutMs,900000);
assert.equal((await request('/api/profile',{occupancyTimeoutMs:3600001})).status,400);
await request('/api/profile',profile);
// Transient sound fixture may raise the displayed score, but must not change tone/output.
await request('/__mock/scenario',{scenario:'impulse'});
assert.equal((await request('/api/status')).body.outputs.cctMireds,profile.normalCCTMireds);
assert.equal((await request('/api/status')).body.outputs.fanPercent,profile.preferredFanPercent);
await request('/__mock/scenario',{scenario:'normal'});
for (const tone of [250,333,454]) {
  assert.equal((await request('/api/profile',{...profile,preferredCCTMireds:tone})).status,200);
  assert.equal((await request('/api/profile')).body.preferredCCTMireds,tone);
  assert.equal((await request('/api/status')).body.outputs.cctMireds,profile.minCCTMireds);
  await request('/__mock/scenario',{scenario:'noise'});
  assert.equal((await request('/api/status')).body.outputs.cctMireds,tone);
  await request('/__mock/scenario',{scenario:'normal'});
}
assert.equal((await request('/api/profile',{...profile,preferredCCTMireds:500})).status,400);
await request('/api/profile',{...profile,preferredBrightness:60,preferredFanPercent:60});
const status=await request('/api/status');
assert.equal(status.body.outputs.fanPercent,60);
await request('/api/light',{power:true,brightness:45,cct:370});
assert.equal((await request('/api/status')).body.outputs.fanPercent,60);
await request('/__mock/scenario',{scenario:'normal'});
await request('/api/fan',{power:true,speed:60});
assert.equal((await request('/api/status')).body.outputs.brightnessPercent,60);
assert.equal((await request('/api/status')).body.outputs.fanPercent,60);
assert.equal((await request('/api/status')).body.system.mode,'MANUAL');
await request('/api/mode',{mode:'OVERRIDE',durationMinutes:10});
const overrideStatus=await request('/api/status');
assert.equal(overrideStatus.body.system.mode,'OVERRIDE');
assert.ok(overrideStatus.body.system.overrideRemainingSeconds > 0 && overrideStatus.body.system.overrideRemainingSeconds <= 600);
assert.equal((await request('/api/mode',{mode:'OVERRIDE',durationMinutes:0})).status,400);
await request('/api/fan',{power:false,speed:0});
assert.equal((await request('/api/status')).body.outputs.fanOn,false);
await request('/__mock/scenario',{scenario:'noise'});
assert.equal((await request('/api/status')).body.outputs.fanPercent,0);
await request('/__mock/scenario',{scenario:'mic-error'});
assert.equal((await request('/api/status')).body.sensor.micValid,false);
await request('/__mock/scenario',{scenario:'vacant'});
assert.equal((await request('/api/status')).body.outputs.fanOn,true); // Demo default keeps automatic output without motion.
await request('/api/profile',{occupancyAutomationEnabled:true});
assert.equal((await request('/api/status')).body.outputs.fanOn,false);
await request('/__mock/scenario',{scenario:'command-error'});
assert.equal((await request('/api/fan',{power:true,speed:40})).status,503);
await request('/__mock/scenario',{scenario:'offline'});
assert.equal((await request('/api/status')).status,503);
assert.equal((await request('/api/diagnostics')).status,503);
await request('/__mock/scenario',{scenario:'normal'});
assert.equal((await request('/api/status')).status,200);
await request('/api/profile',{fanMaxPercent:10});
assert.equal((await request('/api/status')).body.outputs.fanPercent,0); // A cap below 18% disables the fan.
await request('/api/profile',{fanMaxPercent:100});
assert.equal((await request('/api/profile',{preferredBrightness:101})).status,400);
// Setup no longer measures or previews outputs. It preserves normal-output settings.
const before = (await request('/api/profile')).body;
assert.equal((await request('/api/profile',{...before,lightWeight:.8,soundWeight:.75,preferredCCTMireds:417,profileConfigured:true})).status,200);
assert.equal((await request('/api/profile')).body.preferredBrightness,before.preferredBrightness);
assert.equal((await request('/api/profile')).body.preferredFanPercent,before.preferredFanPercent);
assert.equal((await request('/api/profile',{...before,useMeasuredEnvironment:true})).status,400);
await request('/__mock/scenario',{scenario:'noise'});
assert.equal((await request('/api/status')).body.outputs.cctMireds,417);
assert.ok((await request('/api/status')).body.outputs.brightnessPercent < before.preferredBrightness);
await request('/__mock/scenario',{scenario:'normal'});
assert.equal((await request('/api/status')).body.outputs.cctMireds,before.minCCTMireds);
await request('/api/profile',{normalCCTMireds:370,reliefFanPercent:0,reliefHoldMs:10000,recoverConfirmMs:5000});
const custom=(await request('/api/profile')).body;
assert.equal(custom.normalCCTMireds,370); assert.equal(custom.reliefHoldMs,10000); assert.equal(custom.recoverConfirmMs,5000);
assert.equal((await request('/api/status')).body.outputs.cctMireds,370);
await request('/__mock/scenario',{scenario:'noise'});
assert.equal((await request('/api/status')).body.outputs.fanPercent,0);
assert.equal((await request('/api/status')).body.outputs.cctMireds,custom.preferredCCTMireds);
for(const body of [{normalCCTMireds:500},{reliefFanPercent:10},{reliefHoldMs:-1},{recoverConfirmMs:0}])
 assert.equal((await request('/api/profile',body)).status,400);
assert.equal((await request('/api/profile')).body.recoverConfirmMs,5000);
await request('/__mock/scenario',{scenario:'normal'});
await request('/api/profile',{...profile,occupancyAutomationEnabled:true});
await request('/__mock/scenario',{scenario:'vacant'});
let absent=(await request('/api/status')).body;
assert.equal(absent.outputs.lightOn,false); assert.equal(absent.outputs.fanOn,false);
await request('/api/profile',{occupancyAutomationEnabled:false});
absent=(await request('/api/status')).body;
assert.equal(absent.sensor.occupied,false); // PIR is still reported honestly.
assert.equal(absent.outputs.lightOn,true); assert.equal(absent.outputs.fanOn,true);
await request('/api/profile',{lightWeight:.8});
assert.equal((await request('/api/profile')).body.occupancyAutomationEnabled,false); // omitted retains stored setting
assert.equal((await request('/api/diagnostics')).body.profile.occupancyAutomationEnabled,false);
for (const [scenario, expected] of [
  ['normal', {boosting:false,outputValid:true,stabilizationWaiting:false,controlInputsSettled:true}],
  ['fan-boost', {boosting:true,outputValid:true,stabilizationWaiting:true,controlInputsSettled:false}],
  ['fan-pwm-error', {boosting:false,outputValid:false,stabilizationWaiting:true,controlInputsSettled:false,lastError:-5}],
  ['fan-settling', {boosting:false,outputValid:true,stabilizationWaiting:true,controlInputsSettled:false}]
]) {
  await request('/__mock/scenario',{scenario});
  const fan=(await request('/api/diagnostics')).body.fan;
  for (const [key,value] of Object.entries(expected)) assert.equal(fan[key],value);
}
await request('/__mock/scenario',{scenario:'legacy-diagnostics'});
assert.equal((await request('/api/diagnostics')).body.fan,undefined); // Older response is information-absent, not normal.
await request('/__mock/scenario',{scenario:'vacant'});
for(const value of [0,1,'false',null]) assert.equal((await request('/api/profile',{occupancyAutomationEnabled:value})).status,400);
await request('/api/profile',{occupancyAutomationEnabled:true});
assert.equal((await request('/api/status')).body.outputs.lightOn,false);
await request('/__mock/scenario',{scenario:'normal'});
assert.equal((await request('/api/status')).body.outputs.lightOn,true);
assert.equal((await request('/api/status')).body.outputs.fanOn,true);
await request('/api/light',{power:false,brightness:0,cct:333});
await request('/api/fan',{power:false,speed:0});
assert.equal((await request('/api/status')).body.system.mode,'MANUAL');
assert.equal((await request('/api/status')).body.outputs.lightOn,false);
assert.equal((await request('/api/status')).body.outputs.fanOn,false);
Date.now = realNow;
console.log('Mock API scenarios, sensitivity/tone-only setup, normal output preservation and relief tone passed');
