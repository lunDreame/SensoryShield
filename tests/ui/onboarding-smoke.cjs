const assert = require('node:assert/strict');
const path = require('node:path');
const { chromium } = require(process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES
  ? path.join(process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES, 'playwright') : 'playwright');
const base = process.env.TEST_BASE_URL || 'http://127.0.0.1:5174';
(async () => {
  const browser = await chromium.launch({headless:true, channel:process.env.PLAYWRIGHT_CHANNEL});
  try {
    const context = await browser.newContext({viewport:{width:1280,height:900}});
    const page = await context.newPage();
    const errors = [], writes = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => { if(request.method()==='POST') writes.push(new URL(request.url()).pathname); });
    const initial = await (await page.request.get(base+'/api/profile')).json();
    await page.request.post(base+'/api/profile',{data:{...initial,profileConfigured:true}});
    await page.request.post(base+'/api/mode',{data:{mode:'MANUAL'}});
    let failProfile = true;
    await page.route('**/api/profile', async route => {
      if(route.request().method()==='GET' && failProfile) {
        await route.fulfill({status:503,contentType:'application/json',body:'{}'});
      } else await route.continue();
    });
    await page.goto(base);
    await page.getByRole('heading',{name:'기기 설정을 불러오지 못했어요'}).waitFor();
    assert.equal(await page.getByRole('heading',{name:'우리 집 환경'}).count(),0);
    failProfile=false;
    await page.getByRole('button',{name:'다시 시도',exact:true}).click();
    await page.getByRole('heading',{name:'갑자기 밝아질 때 얼마나 불편한가요?'}).waitFor();
    assert.deepEqual(writes,[]); // Opening the page does not alter outputs/mode.
    await page.getByRole('button',{name:/쉽게 불편해져요/}).click();
    await page.getByRole('button',{name:'다음',exact:true}).click();
    await page.getByRole('button',{name:/거의 불편하지 않아요/}).click();
    await page.getByRole('button',{name:'다음',exact:true}).click();
    await page.getByRole('button',{name:'중간 빛',exact:true}).click();
    await page.getByRole('button',{name:'저장하고 자동 시작',exact:true}).click();
    await page.getByRole('heading',{name:'우리 집 환경'}).waitFor();
    assert.deepEqual(writes,['/api/profile','/api/mode']);
    const profile = await (await page.request.get(base+'/api/profile')).json();
    assert.equal(profile.lightWeight,.8); assert.equal(profile.soundWeight,.25);
    assert.equal(profile.preferredCCTMireds,333); assert.equal(profile.reliefFanPercent,0);
    for(const key of ['preferredBrightness','preferredFanPercent','reliefHoldMs','recoverConfirmMs','occupancyTimeoutMs','occupancyAutomationEnabled'])
      assert.equal(profile[key],initial[key]);
    assert.equal((await (await page.request.get(base+'/api/status')).json()).system.mode,'AUTO');
    await page.request.post(base+'/__mock/scenario',{data:{scenario:'noise'}});
    await page.waitForFunction(()=>document.querySelector('.sound-metric strong')?.textContent==='15.000%');
    const relieved = await (await page.request.get(base+'/api/status')).json();
    assert.equal(relieved.outputs.fanPercent,0); assert.equal(relieved.outputs.cctMireds,333);
    await page.request.post(base+'/__mock/scenario',{data:{scenario:'mic-error'}});
    await page.getByText('측정 불가',{exact:true}).waitFor();
    await page.getByRole('button',{name:'기기 상태',exact:true}).click();
    await page.getByText('수신 복구 중',{exact:true}).waitFor();
    await page.request.post(base+'/__mock/scenario',{data:{scenario:'normal'}});
    await page.getByText('정상 수신',{exact:true}).waitFor();
    // A separate browser/computer and a fresh page visit both start with setup.
    const other = await browser.newContext({viewport:{width:375,height:812}});
    const phone = await other.newPage();
    await phone.goto(base);
    await phone.getByRole('heading',{name:'갑자기 밝아질 때 얼마나 불편한가요?'}).waitFor();
    assert.equal(await phone.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),true);
    await page.reload();
    await page.getByRole('heading',{name:'갑자기 밝아질 때 얼마나 불편한가요?'}).waitFor();
    assert.deepEqual(errors,[]);
    await page.request.post(base+'/__mock/scenario',{data:{scenario:'normal'}});
    console.log('onboarding: stored profile, failed load/retry, save then AUTO, settings preservation, relief tone/fan OFF, sound display and new browser passed');
  } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exit(1);});
