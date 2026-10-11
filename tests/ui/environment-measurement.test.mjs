import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
const { measureEnvironment, summarizeEnvironment, ENVIRONMENT_SAMPLE_COUNT } = await import(pathToFileURL(process.argv[2]));
const originalTimeout = globalThis.setTimeout;
globalThis.setTimeout = (fn) => originalTimeout(fn, 0);
const base = {sensor:{lux:100,soundEnergy:0.01,illuminanceValid:true,micValid:true},
  outputs:{lightOn:true,brightnessPercent:60,cctMireds:370,rgbMode:false,red:255,green:255,blue:255,fanOn:true,fanPercent:60},
  system:{ready:true,mode:'AUTO',overrideRemainingSeconds:0}};
let requests, reads, invalid, restoreFails;
function reset() { requests=[]; reads=0; invalid=false; restoreFails=false; }
globalThis.fetch = async (path, init) => {
  const body=init?.body ? JSON.parse(init.body) : undefined;
  requests.push({path,body});
  if (path === '/api/status') {
    reads++;
    return new Response(JSON.stringify({...base, sensor:{...base.sensor,micValid:!(invalid && reads>1)},
      system:{...base.system,mode:reads===1 ? 'AUTO' : 'MANUAL'}}));
  }
  return new Response(JSON.stringify({ok: !(restoreFails && path==='/api/mode')}));
};
reset();
let progress=0;
const result=await measureEnvironment(count=>progress=count);
assert.equal(progress,ENVIRONMENT_SAMPLE_COUNT);
assert.equal(result.luxMedian,100);
assert.equal(result.luxMad,0);
assert.equal(requests.at(-1).body.mode,'AUTO');
assert.equal(requests.find(x=>x.path==='/api/fan').body.speed,60);
assert.equal(requests.find(x=>x.path==='/api/light').body.brightness,60);
reset(); invalid=true;
await assert.rejects(measureEnvironment(()=>{}),/센서/);
assert.equal(requests.at(-1).body.mode,'AUTO');
reset(); restoreFails=true;
await assert.rejects(measureEnvironment(()=>{}),/이전 작동 방식/);
assert.throws(()=>summarizeEnvironment([]),/센서/);
const outlier=Array.from({length:ENVIRONMENT_SAMPLE_COUNT},(_,i)=>({...base,sensor:{...base.sensor,lux:i===0?1000:100}}));
assert.equal(summarizeEnvironment(outlier).luxMedian,100);
globalThis.setTimeout=originalTimeout;
console.log('Environment measurement success, sensor failure, restore failure, and median tests passed');
