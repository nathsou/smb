#!/usr/bin/env node
// Runs tests/web/cartridge_gpu.html in headless Chromium (Playwright) and
// fails unless the WebGL renderer matches the software renderer on every
// compared frame and every theme draws the chosen frames without WebGL
// errors. Build web/runtime-cartridge.wasm first (make runtime-cartridge-wasm),
// and the game module with the recompiler's --wasm.
//
// usage: node tests/web/cartridge_gpu.cjs GAME.wasm ROM.nes RECORDING MAP
//            [every] [frames] [output directory]
// e.g. ... 4 900,2000 /tmp/themes saves each theme's frames as PNGs.
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..', '..');
const [gamePath, romPath, recPath, mapPath, every = '1', frames = '', output] = process.argv.slice(2);
const served = { '/__game': gamePath, '/__rom': romPath, '/__rec': recPath, '/__map': mapPath };
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };

const server = http.createServer((request, response) => {
    const pathname = decodeURIComponent(new URL(request.url, 'http://x').pathname);
    const file = served[pathname] ?? path.join(root, pathname);
    if (!(pathname in served) && !file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
        response.writeHead(404).end();
        return;
    }
    response.writeHead(200, { 'Content-Type': types[path.extname(file)] ?? 'application/octet-stream' });
    fs.createReadStream(file).pipe(response);
});

server.listen(0, '127.0.0.1', async () => {
    const browser = await chromium.launch({ args: ['--enable-unsafe-swiftshader', '--use-angle=swiftshader'] });
    try {
        const page = await browser.newPage();
        page.on('console', message => {
            if (!message.text().includes('GL Driver Message')) console.log('[page]', message.text());
        });
        page.setDefaultTimeout(0);
        await page.goto(`http://127.0.0.1:${server.address().port}/tests/web/cartridge_gpu.html?every=${every}&frames=${frames}`);
        const result = await page.evaluate(() => window.result);
        if (result.error) {
            console.log(result.error);
            process.exitCode = 1;
            return;
        }
        if (output) {
            fs.mkdirSync(output, { recursive: true });
            for (const { frame, theme, url } of result.images) {
                fs.writeFileSync(path.join(output, `${frame}-${theme}.png`),
                    Buffer.from(url.slice(url.indexOf(',') + 1), 'base64'));
            }
        }
        const { images, ...summary } = result;
        console.log(JSON.stringify({ ...summary, images: images.length }));
        process.exitCode = result.compared > 0 && result.mismatched === 0 && result.errors.length === 0 ? 0 : 1;
    } finally {
        await browser.close();
        server.close();
    }
});
