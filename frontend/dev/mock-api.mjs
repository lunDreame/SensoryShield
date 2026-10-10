// Vite development middleware only. Fixtures illustrate UI states, not firmware predictions.
export function mockApi() {
  let scenario = "normal";
  let mode = "AUTO";
  let manualFan = { fanOn: false, fanPercent: 0 };
  let manualLight = { lightOn: true, brightnessPercent: 60, cctMireds: 370, rgbMode: false, red: 255, green: 255, blue: 255 };
  let overrideUntil = 0;
  const started = Date.now();
  let profile = { lightWeight: 0.55, soundWeight: 0.45, minBrightness: 1, maxBrightness: 100,
    minCCTMireds: 250, maxCCTMireds: 454, fanMaxPercent: 100, occupancyTimeoutMs: 30000,
    profileConfigured: false };
  const scenarios = ["normal", "noise", "impulse", "mic-error", "vacant", "offline", "command-error"];
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
          mode = "AUTO";
          send(200,{ok:true,scenario}); return;
        }
        if (scenario === "offline") {send(503,{ok:false});return;}
        if (scenario === "command-error" && req.method === "POST") {send(503,{ok:false});return;}
        if (mode === "OVERRIDE" && Date.now() >= overrideUntil) mode = "AUTO";
        const noise = scenario === "noise";
        const micError = scenario === "mic-error";
        const vacant = scenario === "vacant";
        const impulse = scenario === "impulse" && Math.floor(Date.now()/1500)%4 === 0;
        const autoSpeed = vacant || profile.fanMaxPercent === 0 ? 0 : Math.min(profile.fanMaxPercent, noise || micError ? 20 : 60);
        if (path === "/api/profile") {
          if (req.method === "POST") profile = {...profile, ...body};
          send(200,req.method === "POST" ? {ok:true} : profile);return;
        }
        if (path === "/api/mode") {
          if (!["AUTO","MANUAL","OVERRIDE","SAFE"].includes(body.mode)) {send(400,{ok:false});return;}
          if (body.mode === "OVERRIDE" && (!Number.isInteger(body.durationMinutes) || body.durationMinutes < 1 || body.durationMinutes > 1440)) {send(400,{ok:false});return;}
          mode=body.mode;overrideUntil=Date.now()+(body.durationMinutes ?? 15)*60000;send(200,{ok:true});return;
        }
        if (path === "/api/fan") {
          if (mode !== "MANUAL" && mode !== "OVERRIDE") {
            manualLight={lightOn:!vacant,brightnessPercent:vacant?0:60,cctMireds:370,rgbMode:false,red:255,green:255,blue:255};
          }
          const speed = Math.min(profile.fanMaxPercent, Math.max(0,body.speed));
          manualFan={fanOn:body.power && speed>0,fanPercent:body.power ? speed : 0};
          mode="MANUAL";send(200,{ok:true});return;
        }
        if (path === "/api/light") {
          if (mode !== "MANUAL" && mode !== "OVERRIDE") {
            manualFan={fanOn:autoSpeed>0,fanPercent:autoSpeed};
          }
          manualLight={lightOn:body.power,brightnessPercent:body.brightness,cctMireds:body.cct,rgbMode:!!body.rgbMode,
            red:body.red ?? 255,green:body.green ?? 255,blue:body.blue ?? 255};
          mode="MANUAL";send(200,{ok:true});return;
        }
        const status = {
          sensor:{lux:180,occupied:!vacant,soundEnergy:micError ? 0 : noise || impulse ? 0.15 : 0.01,
            sensoryScore:noise || impulse ? 6 : 0.4,illuminanceValid:true,micValid:!micError,pirValid:true},
          outputs: mode === "MANUAL" || mode === "OVERRIDE" ? {...manualLight,...manualFan} : mode === "SAFE" ?
            {lightOn:false,brightnessPercent:0,cctMireds:370,rgbMode:false,red:255,green:255,blue:255,fanOn:false,fanPercent:0} :
            {lightOn:!vacant,brightnessPercent:vacant?0:60,cctMireds:370,rgbMode:false,red:255,green:255,blue:255,fanOn:autoSpeed>0,fanPercent:autoSpeed},
          system:{ready:true,mode,overrideRemainingSeconds:mode === "OVERRIDE" ? Math.max(0,Math.ceil((overrideUntil-Date.now())/1000)) : 0,
            uptimeSeconds:Math.floor((Date.now()-started)/1000),firmware:"mock-ui-fixtures",
            matter:{commissioned:false,fabricCount:0,threadAttached:false},storage:{appConfig:true,deviceTable:true}}
        };
        if(path === "/api/status") send(200,status);
        else if(path === "/api/diagnostics") send(200,{...status.system,profile});
        else send(404,{ok:false});
      });
    }
  };
}
