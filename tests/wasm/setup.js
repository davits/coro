
beforeAll(async () => {
    const createModule = require('./coro_tests.js');
    coro = await createModule();
})
