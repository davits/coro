require('../setup');

function sleep(ms, value) {
    return new Promise(resolve => {
        setTimeout(resolve, ms, value);
    });
}

test("Simple test", async () => {
    const wasmSleep = coro.sleepAndReturn();
    const faster = sleep(50, 11);
    const first = await Promise.race([wasmSleep, faster]);
    expect(first).toBe(11);
    const answer = await wasmSleep;
    expect(answer).toBe(42);
})
