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

class CustomError extends Error {
    constructor(msg) {
        super(msg)
        this.name = "CustomError";
    }
}

globalThis.CustomError = CustomError;

test("Exception", async () => {
    await coro.failingTask().then(
        result => {
            throw new Error(`The failingTask should throw, but got result: ${result}`);
        },
        error => {
            expect(error).toBeInstanceOf(WebAssembly.Exception);
            expect(error.message).toStrictEqual(["CustomError", "test error"]);
        }
    )
    coro.registerExceptionTranslator();
    await coro.failingTask().then(
        result => {
            throw new Error(`The failingTask should throw, but got result: ${result}`);
        },
        error => {
            expect(error).toBeInstanceOf(CustomError);
            expect(error.name).toStrictEqual("CustomError");
            expect(error.message).toStrictEqual("test error");
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

test("CoroAll", async() => {
    const task = coro.coroAllTask(false);
    const answer = await Promise.race([task, sleep(300, 11)]);
    expect(answer).toBe(5 * 42);

    await coro.coroAllTask(true).then(
        result => {
            throw new Error(`The failingTask should throw, but got result: ${result}`);
        },
        error => {
            expect(error).toBeInstanceOf(CustomError);
            expect(error.name).toStrictEqual("CustomError");
            expect(error.message).toStrictEqual("test error");
        }
    )
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
            expect(error.message).toStrictEqual(["coro::StopError", "Stop was requested via stop token."]);
        }
    );
})

test("AbortController", async () => {
    expect(await coro.testAbortController()).toBe(42);
    expect(await coro.testAbortControllerTimeout()).toBe(42);
})

test("CustomPromiseCancellation", async () => {
    await coro.testCustomPromiseCancellation();
})
