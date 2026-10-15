// Vite development middleware only. Fixtures illustrate UI states, not firmware predictions.
// 시연 시간은 펌웨어와 동일: 유지 5초 / 복귀 확인 3초 / 재실 유지 30초.
// 실사용 시작 예시(응답용 모의값에는 적용하지 않음): reliefHoldMs: 1800000,
// recoverConfirmMs: 3000(공통 공학 설정·검증 대상), occupancyTimeoutMs: 900000.
export function mockApi() {
  const normalizeFan = (requested, cap) => requested <= 0 || cap < 18 ? 0 : Math.min(cap, Math.max(18, requested));
  let scenario = "normal";
  let mode = "AUTO";
  let manualFan = { fanOn: false, fanPercent: 0 };
  let manualLight = { lightOn: true, brightnessPercent: 60, cctMireds: 250, rgbMode: false, red: 255, green: 255, blue: 255 };
  let overrideUntil = 0;
  let child = null;
  const started = Date.now();
  let profile = { lightWeight: 0.55, soundWeight: 0.45, minBrightness: 30, maxBrightness: 100,
    minCCTMireds: 250, maxCCTMireds: 454, preferredCCTMireds: 454, preferredBrightness: 100, preferredFanPercent: 100, fanMaxPercent: 100, occupancyTimeoutMs: 30000, occupancyAutomationEnabled: false,
    normalCCTMireds:250, reliefFanPercent:0, reliefHoldMs:5000, recoverConfirmMs:3000, profileConfigured: false };
  let initialEnvironment = null;
  let measurement = {state:"idle",samples:0,requiredSamples:31,luxMedian:0,luxMad:0,soundMedian:0,soundMad:0};
  let measurementStarted = 0;
  let measuredLight = null;
  const scenarios = ["normal", "warming-up", "noise", "impulse", "mic-error", "vacant", "offline", "command-error", "fan-boost", "fan-pwm-error", "fan-settling", "legacy-diagnostics"];
  const panel = `<!doctype html><html lang="ko"><meta charset="utf-8"><title>Mock 시나리오</title>
  <body style="font:18px system-ui;max-width:700px;margin:50px auto;padding:20px"><h1>UI 시험 시나리오</h1>
  <p>실제 장치가 아닌 개발용 응답입니다. 알고리즘 검증은 C++ 시험으로 수행합니다.</p>
  <select id="scenario">${scenarios.map(x=>`<option>${x}</option>`).join("")}</select>
  <button id="apply">시나리오 적용</button><p id="result" role="status"></p><a href="/">화면 열기</a>
  <script>document.getElementById('apply').onclick=async()=>{const scenario=document.getElementById('scenario').value;const r=await fetch('/__mock/scenario',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({scenario})});document.getElementById('result').textContent=r.ok?'적용됨: '+scenario:'적용 실패';};</script></body></html>`;
  return {
    name: "sensoryshield-mock-api",
    apply: "serve",
    transformIndexHtml(html) {
      return html.replace('<body>', '<body><div style="padding:12px;background:#fff0ce;color:#392400;font:14px system-ui;text-align:center">개발용 Mock 데이터 · <a href="/__mock" target="_blank" rel="noopener">시험 시나리오 변경</a></div>');
    },
    configureServer(server) {
      server.middlewares.use(async (req, res, next) => {
        const path = req.url?.split("?")[0];
        if (path !== "/__mock" && path !== "/__mock/scenario" && !path?.startsWith("/api/")) return next();
        function send(status, value) {
          res.statusCode = status;
          res.setHeader("Content-Type", "application/json");
          res.end(JSON.stringify(value));
        }
        if (path === "/__mock") {res.setHeader("Content-Type", "text/html; charset=utf-8"); res.end(panel); return;}
        let body = {};
        if (req.method === "POST") {
          try {let raw = ""; for await (const chunk of req) raw += chunk; body = JSON.parse(raw);}
          catch {send(400, {ok:false}); return;}
        }
        if (path === "/__mock/scenario") {
          if (!scenarios.includes(body.scenario)) {send(400,{ok:false}); return;}
          scenario = body.scenario;
          mode = "AUTO"; child = null;
          send(200,{ok:true,scenario}); return;
        }
        if (scenario === "offline") {send(503,{ok:false});return;}
        if (scenario === "command-error" && req.method === "POST") {send(503,{ok:false});return;}
        if (mode === "OVERRIDE" && !child && Date.now() >= overrideUntil) mode = "AUTO";
        const noise = scenario === "noise";
        const micError = scenario === "mic-error";
        const vacant = scenario === "vacant";
        // The vacant fixture represents an already-expired hold; real timing is covered in C++ tests.
        const autoAbsent = vacant && profile.occupancyAutomationEnabled;
        const impulse = scenario === "impulse" && Math.floor(Date.now()/1500)%4 === 0;
        const autoCct = noise ? profile.preferredCCTMireds : profile.normalCCTMireds;
        const autoBrightness = autoAbsent ? 0 : noise ? Math.round(profile.preferredBrightness - (profile.preferredBrightness - profile.minBrightness) * .75) : profile.preferredBrightness;
        const normalFan = normalizeFan(profile.preferredFanPercent, profile.fanMaxPercent);
        const reliefFan = normalizeFan(profile.reliefFanPercent, profile.fanMaxPercent);
        const autoSpeed = autoAbsent ? 0 : noise || micError ? Math.min(normalFan, reliefFan) : normalFan;
        const normalOutput={lightOn:profile.preferredBrightness>0,brightnessPercent:profile.preferredBrightness,cctMireds:profile.normalCCTMireds,
          rgbMode:false,red:255,green:255,blue:255,fanOn:normalFan>0,fanPercent:normalFan};
        const blend=(a,b,f)=>{const t={...b};for(const key of ["brightnessPercent","cctMireds","fanPercent"])t[key]=Math.round(a[key]+(b[key]-a[key])*f);
          t.fanPercent=t.fanPercent<18 && b.fanPercent===0?0:normalizeFan(t.fanPercent,profile.fanMaxPercent);
          t.lightOn=t.brightnessPercent>0;t.fanOn=t.fanPercent>0;return t;};
        if(child) {
          const now=Date.now(), elapsed=now-child.start;
          if(elapsed<5000) child.output=blend(child.from,child.to,elapsed/5000);
          else {
            child.output=child.output ?? child.to;
            if(!child.recovering) child.output=child.to;
            if(noise || micError) {child.lowSince=null;child.recovering=false;child.to=child.output;}
            else if(child.lowSince===null) child.lowSince=Math.max(child.start+5000,now);
            if(elapsed>=10000) {
              mode="AUTO";
              if(child.lowSince!==null && now-child.lowSince>=profile.recoverConfirmMs) {
                if(!child.recovering){child.recovering=true;child.recoverStart=now;child.from=child.output;}
                child.output=blend(child.from,normalOutput,Math.min(1,(now-child.recoverStart)/5000));
                if(now-child.recoverStart>=5000) child=null;
              }
            }
          }
        }
        if(path === "/api/comfort") {
          if(req.method!=="POST" || mode==="SAFE" || !["BRIGHT","DARK","LOUD","QUIET"].includes(body.action)){send(400,{ok:false});return;}
          const from=child?.output ?? (mode==="MANUAL" || mode==="OVERRIDE" ? {...manualLight,...manualFan} : {...normalOutput,
            brightnessPercent:autoBrightness,cctMireds:autoCct,fanPercent:autoSpeed,lightOn:autoBrightness>0,fanOn:autoSpeed>0});
          const to={...from}, action=body.action;
          const light=from.lightOn?from.brightnessPercent:0,fan=from.fanOn?from.fanPercent:0;
          if(action==="BRIGHT" || action==="LOUD") to.brightnessPercent=Math.min(light,profile.minBrightness);
          if(action==="DARK") to.brightnessPercent=light>=profile.maxBrightness?light:Math.min(profile.maxBrightness,Math.max(profile.minBrightness,light+30));
          if(to.brightnessPercent!==from.brightnessPercent){to.cctMireds=profile.preferredCCTMireds;to.rgbMode=false;}
          if(action==="LOUD") to.fanPercent=Math.min(fan,normalizeFan(profile.reliefFanPercent,profile.fanMaxPercent));
          if(action==="QUIET") to.fanPercent=normalizeFan(fan+30,profile.fanMaxPercent);
          to.lightOn=to.brightnessPercent>0;to.fanOn=to.fanPercent>0;
          if(JSON.stringify(from)===JSON.stringify(to)){send(200,{ok:true,changed:false});return;}
          child={start:Date.now(),from,to,output:from,lowSince:null,recovering:false};mode="OVERRIDE";
          send(200,{ok:true});return;
        }
        if (measurement.state === "collecting") {
          if (Date.now() - measurementStarted >= 15000 && micError) measurement.state = "error";
          else if (!micError) {
            measurement.samples = Math.min(31, Math.floor((Date.now()-measurementStarted)/200));
            measurement.luxMedian = 100 + measuredLight.brightnessPercent * .5;
            measurement.luxMad = .5; measurement.soundMedian = noise ? .15 : .01; measurement.soundMad = .001;
            if (measurement.samples === 31) measurement.state = "ready";
          }
        }
        if (path === "/api/environment-measurement") {
          if (req.method === "POST") {
            if (body.action === "start" && mode !== "SAFE") {
              measurement = {state:"collecting",samples:0,requiredSamples:31,luxMedian:0,luxMad:0,soundMedian:0,soundMad:0};
              if (mode !== "MANUAL" && mode !== "OVERRIDE") {
                manualLight={lightOn:!autoAbsent && profile.preferredBrightness > 0,brightnessPercent:autoBrightness,cctMireds:autoCct,rgbMode:false,red:255,green:255,blue:255};
                manualFan={fanOn:autoSpeed>0,fanPercent:autoSpeed};
              }
              mode = "MANUAL";
              measurementStarted = Date.now(); measuredLight = {...manualLight};
            } else if (body.action === "cancel") measurement.state = "idle";
            else {send(400,{ok:false});return;}
          }
          send(200,{...measurement,hasSavedInitialEnvironment:!!initialEnvironment,savedLuxMedian:initialEnvironment?.luxMedian ?? 0,savedSoundMedian:initialEnvironment?.soundMedian ?? 0});return;
        }
        if (path === "/api/profile") {
          if (req.method === "POST") {
            const next = {...profile, ...body};
            if (!Number.isInteger(next.occupancyTimeoutMs) || next.occupancyTimeoutMs < 1000 || next.occupancyTimeoutMs > 3600000) {send(400,{ok:false});return;}
            if (typeof next.occupancyAutomationEnabled !== "boolean") {send(400,{ok:false}); return;}
            if (!Number.isInteger(next.preferredCCTMireds) || next.preferredCCTMireds < next.minCCTMireds || next.preferredCCTMireds > next.maxCCTMireds) {
              send(400, {ok:false}); return;
            }
            if (!Number.isInteger(next.preferredBrightness) || next.preferredBrightness < next.minBrightness || next.preferredBrightness > next.maxBrightness) {
              send(400,{ok:false});return;
            }
            if (body.preferredFanPercent !== undefined && (!Number.isInteger(next.preferredFanPercent) || next.preferredFanPercent < 0 || next.preferredFanPercent > next.fanMaxPercent)) {
              send(400,{ok:false});return;
            }
            if (!Number.isInteger(next.normalCCTMireds) || next.normalCCTMireds < next.minCCTMireds || next.normalCCTMireds > next.maxCCTMireds ||
                !Number.isInteger(next.reliefFanPercent) || next.reliefFanPercent < 0 || next.reliefFanPercent > 100 || (next.reliefFanPercent > 0 && next.reliefFanPercent < 18) ||
                !Number.isInteger(next.reliefHoldMs) || next.reliefHoldMs < 0 || next.reliefHoldMs > 3600000 ||
                !Number.isInteger(next.recoverConfirmMs) || next.recoverConfirmMs < 250 || next.recoverConfirmMs > 60000) {send(400,{ok:false}); return;}
            next.preferredFanPercent = normalizeFan(next.preferredFanPercent, next.fanMaxPercent);
            if (body.useMeasuredEnvironment) { send(400,{ok:false}); return; }
            delete next.useMeasuredEnvironment;
            profile = next; if(child){child=null;mode="AUTO";}
          }
          send(200,req.method === "POST" ? {ok:true} : profile);return;
        }
        if (path === "/api/mode") {
          if (!["AUTO","MANUAL","OVERRIDE","SAFE"].includes(body.mode)) {send(400,{ok:false});return;}
          if (body.mode === "OVERRIDE" && (!Number.isInteger(body.durationMinutes) || body.durationMinutes < 1 || body.durationMinutes > 1440)) {send(400,{ok:false});return;}
          if (body.mode !== "MANUAL" && measurement.state === "collecting") measurement.state="error";
          child=null;mode=body.mode;overrideUntil=Date.now()+(body.durationMinutes ?? 15)*60000;send(200,{ok:true});return;
        }
        if (path === "/api/fan") {
          if (measurement.state === "collecting") measurement.state="error";
          if (mode !== "MANUAL" && mode !== "OVERRIDE") {
            manualLight={lightOn:!autoAbsent && profile.preferredBrightness > 0,brightnessPercent:autoBrightness,cctMireds:autoCct,rgbMode:false,red:255,green:255,blue:255};
          }
          const speed = normalizeFan(Math.max(0, body.speed), profile.fanMaxPercent);
          manualFan={fanOn:body.power && speed>0,fanPercent:body.power ? speed : 0};
          child=null;mode="MANUAL";send(200,{ok:true});return;
        }
        if (path === "/api/light") {
          if (measurement.state === "collecting") measurement.state="error";
          if (mode !== "MANUAL" && mode !== "OVERRIDE") {
            manualFan={fanOn:autoSpeed>0,fanPercent:autoSpeed};
          }
          manualLight={lightOn:body.power,brightnessPercent:body.brightness,cctMireds:body.cct,rgbMode:!!body.rgbMode,
            red:body.red ?? 255,green:body.green ?? 255,blue:body.blue ?? 255};
          child=null;mode="MANUAL";send(200,{ok:true});return;
        }
        const status = {
          sensor:{lux:180,occupied:!vacant,soundEnergy:micError ? 0 : noise || impulse ? 0.15 : 0.01,
            baselineReady:scenario !== "warming-up" && !micError,sensoryScore:noise || impulse ? 3 : 0.4,illuminanceValid:true,micValid:!micError,pirValid:true},
          outputs: child ? child.output : mode === "MANUAL" || mode === "OVERRIDE" ? {...manualLight,...manualFan} : mode === "SAFE" ?
            {lightOn:false,brightnessPercent:0,cctMireds:profile.normalCCTMireds,rgbMode:false,red:255,green:255,blue:255,fanOn:false,fanPercent:0} :
            {lightOn:!autoAbsent && profile.preferredBrightness > 0,brightnessPercent:autoBrightness,cctMireds:autoCct,rgbMode:false,red:255,green:255,blue:255,fanOn:autoSpeed>0,fanPercent:autoSpeed},
          system:{ready:true,mode,overrideRemainingSeconds:mode === "OVERRIDE" ? child ? Math.max(0,Math.ceil((10000-Math.max(5000,Date.now()-child.start))/1000)) : Math.max(0,Math.ceil((overrideUntil-Date.now())/1000)) : 0,
            uptimeSeconds:Math.floor((Date.now()-started)/1000),firmware:"mock-ui-fixtures",
            matter:{commissioned:false,fabricCount:0,threadAttached:false},storage:{appConfig:true,deviceTable:true}}
        };
        if(path === "/api/status") send(200,status);
        else if(path === "/api/diagnostics") {
          const targetPercent = status.outputs.fanOn ? status.outputs.fanPercent : 0;
          const fan = scenario === "fan-boost" ? {targetPercent,appliedDutyPercent:100,boosting:true,outputValid:true,lastError:0,changedAgoMs:100,stabilizationWaiting:true,controlInputsSettled:false} :
            scenario === "fan-pwm-error" ? {targetPercent,appliedDutyPercent:100,boosting:false,outputValid:false,lastError:-5,changedAgoMs:700,stabilizationWaiting:true,controlInputsSettled:false} :
            scenario === "fan-settling" ? {targetPercent,appliedDutyPercent:targetPercent,boosting:false,outputValid:true,lastError:0,changedAgoMs:200,stabilizationWaiting:true,controlInputsSettled:false} :
            {targetPercent,appliedDutyPercent:targetPercent,boosting:false,outputValid:true,lastError:0,changedAgoMs:800,stabilizationWaiting:false,controlInputsSettled:true};
          const microphone = {sampleCount:Math.floor((Date.now()-started)/100),sampleAgeMs:micError ? 1500 : 50,
            restartCount:micError ? 1 : 0,lastError:micError ? -11 : 0,fresh:!micError,recovering:micError};
          send(200,scenario === "legacy-diagnostics" ? {...status.system,profile} : {...status.system,profile,fan,microphone});
        }
        else send(404,{ok:false});
      });
    }
  };
}
