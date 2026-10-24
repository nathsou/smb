#!/usr/bin/env node
// Runs tests/web/themes.html in headless Chromium (Playwright) and fails
// unless every theme draws the chosen frames without WebGL errors. Build
// web/smb.wasm first (make wasm).
//
// usage: NODE_PATH=$(npm root -g) node tests/web/themes.cjs [query] [output directory]
// e.g. 'frames=2000&rom=1' /tmp/themes saves each theme's frame as a PNG.
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..', '..');
const types = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
};

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
    const [query = '', output] = process.argv.slice(2);
    const browser = await chromium.launch({ args: ['--enable-unsafe-swiftshader', '--use-angle=swiftshader'] });
    try {
        const page = await browser.newPage();
        page.on('console', message => {
            if (!message.text().includes('GL Driver Message')) console.log('[page]', message.text());
        });
        await page.goto(`http://127.0.0.1:${server.address().port}/tests/web/themes.html?${query}`);
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
        console.log(JSON.stringify({ images: result.images.length, errors: result.errors }));
        process.exitCode = result.errors.length === 0 && result.images.length > 0 ? 0 : 1;
    } finally {
        await browser.close();
        server.close();
    }
});
