import assert from 'node:assert/strict';
import {mockApi} from '../../frontend/dev/mock-api.mjs';
import {Readable} from 'node:stream';
let middleware;
mockApi().configureServer({middlewares:{use(fn){middleware=fn;}}});
async function request(url,body){
 const req=Readable.from(body?[JSON.stringify(body)]:[]); req.url=url;req.method=body?'POST':'GET';
 let response;const res={statusCode:200,setHeader(){},end(text){response={status:this.statusCode,body:JSON.parse(text)};}};
 await middleware(req,res,()=>{throw Error('unexpected route');});return response;
}
const payload={luxMedianMilli:180000,luxMadMilli:2000,soundMedianMicro:10000,soundMadMicro:5000,samples:20};
assert.equal((await request('/api/environment-baseline')).body.configured,false);
assert.equal((await request('/api/environment-baseline',payload)).status,200);
assert.deepEqual((await request('/api/environment-baseline')).body,{...payload,configured:true});
assert.equal((await request('/api/environment-baseline',{...payload,samples:0})).status,400);
assert.equal((await request('/api/environment-baseline',{...payload,soundMedianMicro:1000001})).status,400);
assert.equal((await request('/api/environment-baseline',{...payload,luxMadMilli:-1})).status,400);
assert.deepEqual((await request('/api/environment-baseline')).body,{...payload,configured:true});
await request('/__mock/scenario',{scenario:'command-error'});
assert.equal((await request('/api/environment-baseline',payload)).status,503);
assert.deepEqual((await request('/api/environment-baseline')).body,{...payload,configured:true});
console.log('baseline mock API read/write, validation, and failed-write preservation passed');
