// Real WebGPU tests. Node has no WebGPU implementation, so the wasm module is
// loaded into headless Chrome (via puppeteer's bundled Chrome for Testing,
// which runs on GitHub Actions runners). SwiftShader supplies a software
// WebGPU adapter, so no physical GPU is required.

const http = require('http');
const path = require('path');
const fs = require('fs');
const puppeteer = require('puppeteer');

const ROOT = path.resolve(__dirname, '..');
const MIME = {
    '.js': 'text/javascript',
    '.wasm': 'application/wasm',
};

// Chrome can't fetch the .wasm over file://, so serve the module dir.
function startServer() {
    return new Promise((resolve) => {
        const server = http.createServer((req, res) => {
            if (req.url === '/') {
                res.writeHead(200, {'Content-Type': 'text/html'});
                res.end('<!doctype html><script src="coro_tests.js"></script>');
                return;
            }
            if (req.url === '/favicon.ico') {
                res.writeHead(204);
                res.end();
                return;
            }
            const file = path.join(ROOT, path.posix.normalize(req.url).replace(/^(\.\.[/\\])+/, ''));
            fs.readFile(file, (err, data) => {
                if (err) {
                    res.writeHead(404);
                    res.end();
                    return;
                }
                res.writeHead(200, {'Content-Type': MIME[path.extname(file)] || 'application/octet-stream'});
                res.end(data);
            });
        });
        server.listen(0, '127.0.0.1', () => resolve(server));
    });
}

let server;
let browser;
let page;

beforeAll(async () => {
    server = await startServer();
    browser = await puppeteer.launch({
        args: [
            '--no-sandbox', // required on GitHub Actions runners
            '--disable-setuid-sandbox',
            '--enable-unsafe-webgpu',
            '--enable-features=Vulkan',
            '--use-webgpu-adapter=swiftshader', // software adapter, no GPU needed
        ],
    });
    page = await browser.newPage();
    page.on('console', (msg) => {
        if (msg.type() === 'error') {
            console.error(`browser: ${msg.text()}`);
        }
    });
    const {port} = server.address();
    await page.goto(`http://127.0.0.1:${port}/`);
    await page.evaluate(async () => {
        window.coro = await createModule();
    });
}, 120000);

afterAll(async () => {
    if (browser) {
        await browser.close();
    }
    if (server) {
        server.close();
    }
});

describe("WebGpu (emdawnwebgpu in headless Chrome)", () => {
    test("browser exposes a WebGPU adapter", async () => {
        const haveAdapter = await page.evaluate(
            async () => !!(navigator.gpu && await navigator.gpu.requestAdapter()));
        expect(haveAdapter).toBe(true);
    }, 60000);

    test("RequestAdapter → RequestDevice chain succeeds", async () => {
        const result = await page.evaluate(() => coro.testRequestAdapterAndDevice());
        expect(result.haveDevice).toBe(true);
    }, 60000);

    test("compute dispatch round-trips through all awaitable wrappers", async () => {
        const count = 128;
        const result = await page.evaluate((n) => coro.testComputeRoundTrip(n), count);
        expect(result).toHaveLength(count);
        // The shader computes 2x + 1; anything else means the dispatch or the
        // readback (OnSubmittedWorkDone/MapAsync) went wrong.
        result.forEach((value, i) => expect(value).toBe(2 * i + 1));
    }, 60000);

    test("broken shader reports diagnostics and trips the error scope", async () => {
        const result = await page.evaluate(() => coro.testShaderDiagnostics());
        expect(result.errors).not.toBe("");
        expect(result.scopeCaughtValidationError).toBe(true);
    }, 60000);
});
