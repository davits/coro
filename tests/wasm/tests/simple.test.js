require('../setup');

function sleep(ms, value) {
    return new Promise(resolve => {
        setTimeout(resolve, ms, value);
    });
}

test("Simple", async () => {
    const promise = coro.sleepyTask();
    const faster = sleep(100, 11);
    const first = await Promise.race([promise, faster]);
    expect(first).toBe(11);
    const answer = await promise;
    expect(answer).toBe(42);
})

test("Exception", async () => {
    await coro.failingTask().then(
        result => {
            throw new Error(`The failingTask should throw, but got result: ${result}`);
        },
        error => {
            expect(error).toBeInstanceOf(WebAssembly.Exception);
            expect(error.message).toStrictEqual(["std::runtime_error", "test error"]);
        }
    )
})

test("Lifetime", async () => {
    const promise = coro.lifetimeTask();
    expect(coro.executorCount()).toBe(1);
    const answer = await promise;
    expect(answer).toBe(42);
    expect(coro.runStateDestroyed()).toBe(true);
})

test("Cancellation", async () => {
    const task = coro.launchCancellableTask();
    let timer = sleep(50, 11);
    const answer = await Promise.race([task.promise(), timer]);
    expect(answer).toBe(11);

    task.cancel();
    timer = sleep(50, 12);
    await Promise.race([task.promise(), timer]).then(
        (result) => {
            throw new Error(`Cancellation should throw, but instead got '${result}'`);
        },
        (error) => {
            expect(error).toBeInstanceOf(WebAssembly.Exception);
            expect(error.message).toStrictEqual(["coro::StopError", undefined]);
        }
    );
})
