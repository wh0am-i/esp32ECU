"""Opcional: pip install playwright; python -m playwright install chromium.
Com simulator.py rodando: python tests/browser_smoke.py
"""
from pathlib import Path
import os
BASE = os.environ.get("ECU_TEST_URL", "http://127.0.0.1:8080")
from playwright.sync_api import sync_playwright, expect

with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page(viewport={'width': 1440, 'height': 1000})
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.goto(BASE)
    expect(page.locator('#connection-state')).to_contain_text('SIMULADOR')
    page.select_option('#scenario', 'overheat')
    expect(page.locator('#overheat-banner')).to_be_visible()
    page.select_option('#scenario', 'idle')
    expect(page.locator('#overheat-banner')).to_be_hidden()
    Path('artifacts').mkdir(exist_ok=True)
    page.screenshot(path='artifacts/dashboard.png', full_page=True)

    page.goto(BASE+'/config.html')
    expect(page.locator('[role=tab]')).to_have_count(5)
    page.get_by_role('tab', name='Mapa de ignição').click()
    page.wait_for_function('mapData !== null')
    assert page.evaluate('mapData.table.length === 16 && mapData.table.every(row => row.length === 16)')
    original = page.evaluate('JSON.stringify(mapData.table)')
    page.locator('[data-row="15"][data-col="0"]').click()
    expect(page.locator('.inline-map-input')).to_be_visible()
    page.locator('.inline-map-input').fill('23.4')
    page.locator('.inline-map-input').press('Enter')
    assert page.evaluate('mapData.table[15][0]') == 23.4
    start = page.locator('[data-row="14"][data-col="1"]').bounding_box()
    end = page.locator('[data-row="12"][data-col="3"]').bounding_box()
    page.mouse.move(start['x']+start['width']/2,start['y']+start['height']/2)
    page.mouse.down()
    page.mouse.move(end['x']+end['width']/2,end['y']+end['height']/2,steps=8)
    page.mouse.up()
    expect(page.locator('td.selected')).to_have_count(9)
    page.locator('.inline-map-input').fill('22.2')
    page.locator('.inline-map-input').press('Enter')
    assert page.evaluate('[12,13,14].every(r=>[1,2,3].every(c=>mapData.table[r][c]===22.2))')
    assert page.evaluate('mapData.table[15][0]') == 23.4
    page.evaluate('applyTableDelta(1)')
    assert page.evaluate('mapData.table[13][2]') == 23.2
    page.get_by_role('button',name='Desfazer',exact=True).click()
    assert page.evaluate('mapData.table[13][2]') == 22.2
    page.locator('[data-row="10"][data-col="5"]').click()
    before = page.evaluate('mapData.table[10][5]')
    page.locator('.inline-map-input').fill('99')
    page.locator('.inline-map-input').press('Escape')
    assert page.evaluate('mapData.table[10][5]') == before
    expect(page.locator('#map-state')).to_contain_text('ainda não gravadas')
    page.evaluate('applyTableDelta(0.5)')
    expect(page.locator('#map-state')).to_contain_text('ainda não gravadas')
    page.once('dialog', lambda d: d.accept())
    page.get_by_role('button', name='Descartar edição do mapa').click()
    page.wait_for_function('(expected) => JSON.stringify(mapData.table) === expected', arg=original)
    page.route('**/api/map', lambda route: route.fulfill(status=500, content_type='application/json', body='{"error":"Falha simulada"}') if route.request.method == 'POST' else route.continue_())
    page.evaluate('applyTableDelta(0.5); saveMapToServer()')
    expect(page.locator('#notification')).to_contain_text('Falha simulada')
    assert page.evaluate('mapDirty')
    page.unroute('**/api/map')
    page.once('dialog', lambda d: d.accept())
    page.get_by_role('button', name='Descartar edição do mapa').click()
    page.locator('#notification').evaluate('(el) => el.hidden = true')
    page.screenshot(path='artifacts/config.png', full_page=True)
    for tab in page.get_by_role('tab').all():
        tab.click()
        assert page.locator('[role=tabpanel]:visible').count() == 1

    page.goto(BASE+'/logs.html')
    page.wait_for_function('samples.length > 0')
    page.get_by_role('button', name='Pausar visualização').click()
    expect(page.locator('#log-source')).to_contain_text('pausada')
    page.locator('#channels input').first.uncheck()
    page.locator('#cursor-slider').fill('0')
    with page.expect_download() as download:
        page.get_by_role('button', name='Exportar CSV').click()
    assert download.value.suggested_filename.endswith('.csv')
    page.locator('#import-log').set_input_files({'name':'test.csv', 'mimeType':'text/csv', 'buffer':b't_ms,rpm,tps,advance,temp\n1000,1000,10,12,80\n1050,2000,20,14,85\n'})
    expect(page.locator('#log-source')).to_contain_text('Arquivo CSV')
    expect(page.locator('#log-count')).to_contain_text('2 amostras')
    expect(page.locator('#range-1')).to_contain_text('2000.0')
    import math
    csv = 't_ms,rpm,tps,advance,temp\n' + '\n'.join(f'{1000+i*50},{2500+1800*math.sin(i/20)},{50+40*math.sin(i/25)},{22+8*math.sin(i/40)},{80+i/30}' for i in range(400))
    page.locator('#import-log').set_input_files({'name':'session.csv','mimeType':'text/csv','buffer':csv.encode()})
    expect(page.locator('#log-count')).to_contain_text('400 amostras')
    page.locator('#channels input').first.check()
    page.get_by_role('button', name='Zoom +',exact=True).click()
    assert page.evaluate('viewRange[1]-viewRange[0]') < 10000
    previous = page.evaluate('viewRange[0]')
    page.locator('#pan-right').click()
    assert page.evaluate('viewRange[0]') > previous
    page.locator('#zoom-reset').click()
    assert page.evaluate('viewRange') is None
    chart = page.locator('#logCanvas').bounding_box()
    page.mouse.move(chart['x']+chart['width']*.3,chart['y']+80)
    page.mouse.down()
    page.mouse.move(chart['x']+chart['width']*.6,chart['y']+80,steps=8)
    page.mouse.up()
    assert page.evaluate('viewRange[1]-viewRange[0]') < 10000
    span = page.evaluate('viewRange[1]-viewRange[0]')
    page.mouse.wheel(0,-100)
    page.wait_for_timeout(150)
    assert page.evaluate('viewRange[1]-viewRange[0]') < span
    page.locator('#zoom-reset').click()
    page.screenshot(path='artifacts/datalogger.png', full_page=True)
    for name in ['index.html', 'config.html', 'logs.html']:
        page.set_viewport_size({'width':390, 'height':844})
        page.goto(BASE+'/'+name)
        expect(page.locator('#connection-state')).to_contain_text('SIMULADOR')
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), name
    page.route('**/api/status', lambda route: route.abort())
    expect(page.locator('#connection-state')).to_contain_text('desatualizados', timeout=10000)
    assert not errors, errors
    browser.close()
    print('OK: desktop, mobile, cenários, abas, mapa pendente, erro HTTP, CSV e desconexão.')
