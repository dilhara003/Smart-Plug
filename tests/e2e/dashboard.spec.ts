import { test, expect } from '@playwright/test';
test('demo controls, settings, navigation and activity work',async({page})=>{
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('/');await page.getByRole('button',{name:'Explore the demo'}).click();
  await expect(page.getByRole('heading',{name:'Your space, at a glance.'})).toBeVisible();
  const power=page.getByRole('switch',{name:'Plug power',exact:true});await expect(power).toHaveAttribute('aria-checked','true');
  await power.click();await expect(power).toHaveAttribute('aria-checked','false');
  await page.getByRole('button',{name:'Plug settings',exact:true}).click();
  const modal=page.getByRole('dialog');await modal.getByLabel('Presence / override timeout (seconds)').fill('450');
  await modal.getByRole('button',{name:'Send settings to device'}).click();await expect(modal).toHaveCount(0);
  await page.getByRole('button',{name:'Automations',exact:true}).click();
  await expect(page.getByLabel('Presence / override timeout (seconds)')).toHaveValue('450');
  await page.getByRole('button',{name:'Activity',exact:true}).click();
  await expect(page.getByRole('heading',{name:'Command history'})).toBeVisible();
  await expect(page.getByText('Smart plug · OFF',{exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Settings',exact:true}).click();await page.getByLabel('Device name',{exact:true}).fill('Studio bench');await page.getByRole('button',{name:'Save name'}).click();
  await expect(page.getByRole('combobox',{name:'Selected device'})).toContainText('Studio bench');
  expect(errors).toEqual([]);
});
for(const width of [320,390,1440])test(`responsive layout at ${width}px`,async({page})=>{
  await page.setViewportSize({width,height:1000});await page.goto('/');await page.getByRole('button',{name:'Explore the demo'}).click();
  await expect(page.getByRole('heading',{name:'Your space, at a glance.'})).toBeVisible();
  await expect(page.getByRole('region',{name:'Current draw monitor'})).toBeVisible();
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth)).toBe(true);
  await page.getByRole('region',{name:'Current draw monitor'}).screenshot({path:`artifacts/current-monitor-${width}.png`,animations:'disabled'});
  if(width<800){await page.getByRole('button',{name:'Open navigation'}).click();await page.getByRole('navigation',{name:'Main navigation'}).getByRole('button',{name:/^Devices/}).click();await expect(page.getByRole('heading',{name:'A home for every device.'})).toBeVisible();}
  await page.screenshot({path:`artifacts/dashboard-${width}.png`,fullPage:true,animations:'disabled'});
});

test('current monitor converts units, changes history range and inspects real sample values',async({page})=>{
  await page.goto('/');await page.getByRole('button',{name:'Explore the demo'}).click();
  const monitor=page.getByRole('region',{name:'Current draw monitor'});
  await expect(monitor.getByTestId('current-reading')).toHaveText('0.420A');
  const slider=monitor.getByRole('slider',{name:'Inspect current measurements'});
  const recentCount=Number(await slider.getAttribute('max'));
  await monitor.getByRole('button',{name:'Milliamps',exact:true}).click();
  await expect(monitor.getByTestId('current-reading')).toHaveText('420mA');
  await monitor.getByRole('button',{name:'1 hour',exact:true}).click();
  await expect(monitor.getByRole('img',{name:/Current measurements/})).toHaveAttribute('aria-label',/last 1 hour, in mA/);
  expect(Number(await slider.getAttribute('max'))).toBeGreaterThan(recentCount);
  await slider.focus();await slider.press('Home');
  await expect(slider).toHaveValue('0');
  await expect(monitor.getByRole('status')).toBeVisible();
  await expect(slider).toHaveAttribute('aria-valuetext',/mA at/);
  await monitor.getByRole('button',{name:'24 hours',exact:true}).click();
  await expect(monitor.getByRole('img',{name:/Current measurements/})).toHaveAttribute('aria-label',/last 24 hours/);
  await monitor.getByRole('button',{name:'Amps',exact:true}).click();
  await expect(monitor.getByTestId('current-reading')).toHaveText('0.420A');
  await page.screenshot({path:'artifacts/current-monitor-demo.png',fullPage:true,animations:'disabled'});
});
