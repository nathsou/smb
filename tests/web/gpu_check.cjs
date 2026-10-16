#!/usr/bin/env node
// Runs tests/web/gpu_check.html in headless Chromium (Playwright) and fails
// unless the WebGL renderer matches the software renderer on every compared
// frame. Build web/smb.wasm first (make wasm).
//
// usage: NODE_PATH=$(npm root -g) node tests/web/gpu_check.cjs [every]
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..', '..');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };

const server = http.createServer((request, response) => {
    const file = path.join(root, decodeURIComponent(new URL(request.url, 'http://x').pathname));
    if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
        response.writeHead(404).end();
        return;
    }
    response.writeHead(200, { 'Content-Type': types[path.extname(file)] ?? 'text/plain' });
    fs.createReadStream(file).pipe(response);
});

server.listen(0, '127.0.0.1', async () => {
    const every = process.argv[2] ?? '1';
    const browser = await chromium.launch({ args: ['--enable-unsafe-swiftshader', '--use-angle=swiftshader'] });
    try {
        const page = await browser.newPage();
        page.on('console', message => {
            if (!message.text().includes('GL Driver Message')) console.log('[page]', message.text());
        });
        await page.goto(`http://127.0.0.1:${server.address().port}/tests/web/gpu_check.html?every=${every}`);
        const result = await page.evaluate(() => window.result);
        console.log(JSON.stringify(result));
        process.exitCode = !result.error && result.compared > 0 && result.mismatched === 0 ? 0 : 1;
    } finally {
        await browser.close();
        server.close();
    }
});
