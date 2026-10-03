const assert = require('node:assert/strict');
const path = require('node:path');
const { chromium } = require(process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES
  ? path.join(process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES, 'playwright') : 'playwright');
(async () => {
  const browser = await chromium.launch({headless:true});
  const page = await browser.newPage({viewport:{width:1280,height:900}});
  const errors=[];
  page.on('pageerror', error => errors.push(error.message));
  const base='http://127.0.0.1:5173';
  async function scenario(name) {
    const r=await page.request.post(base+'/__mock/scenario',{data:{scenario:name}});
    assert.equal(r.status(),200);
  }
  await scenario('normal');
  await page.goto(base);
  const fan=page.locator('section').filter({has:page.getByRole('heading',{name:'팬',exact:true})});
  const slider=fan.locator('input[type=range]');
  await page.getByText('기기 연결됨',{exact:true}).waitFor();
  await slider.waitFor();
  await page.waitForFunction(()=>!document.querySelector('input[min="18"]').disabled);
  await slider.fill('60');
  await page.waitForTimeout(1800); // Survive a telemetry update without losing draft.
  assert.equal(await slider.inputValue(),'60');
  await fan.getByRole('button',{name:'속도 적용'}).click();
  await fan.getByText('수동 제어 중',{exact:false}).waitFor();
  await fan.getByText('동작 · 60%',{exact:true}).waitFor();
  await fan.getByRole('button',{name:'정지',exact:true}).click();
  await fan.getByText('정지',{exact:true}).first().waitFor();
  await scenario('command-error');
  await slider.fill('50');
  await fan.getByRole('button',{name:'속도 적용'}).click();
  await fan.getByRole('alert').waitFor();
  assert.equal(await slider.inputValue(),'50');
  await scenario('offline');
  await page.getByText('기기 연결 실패',{exact:true}).waitFor();
  assert.equal(await slider.isDisabled(),true);
  assert.equal(await fan.getByRole('button',{name:'속도 적용'}).isDisabled(),true);
  await scenario('mic-error');
  await page.getByText('기기 연결됨',{exact:true}).waitFor();
  await page.getByText('측정 불가',{exact:true}).waitFor();
  await scenario('noise');
  await fan.getByText('동작 · 18%',{exact:true}).waitFor();
  await scenario('normal');
  await page.setViewportSize({width:375,height:812});
  await page.waitForTimeout(1800);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),true);
  assert.deepEqual(errors,[]);
  console.log('fan UI: draft, apply, stop, rejection, disconnect, recovery, microphone error, noise, mobile passed');
  await browser.close();
})().catch(error=>{console.error(error);process.exit(1);});
