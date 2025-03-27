addToLibrary({

    _coro_lib_make_promise__deps: ['$Emval'],
    _coro_lib_make_promise__sig: 'ppp',
    _coro_lib_make_promise: (resolveHandlePtr, rejectHandlePtr) => {
        return Emval.toHandle(new Promise((resolve, reject) => {
            {{{ makeSetValue('resolveHandlePtr', '0', 'Emval.toHandle(resolve)', '*') }}};
            {{{ makeSetValue('rejectHandlePtr', '0', 'Emval.toHandle(reject)', '*') }}};
        }));
    },

    _coro_lib_await_promise__deps: ['$Emval', '_coro_lib_awaiter_resolve', '_coro_lib_awaiter_reject'],
    _coro_lib_await_promise__sig: 'vpp',
    _coro_lib_await_promise: (promiseHandle, awaiterPtr) => {
        const promise = Emval.toValue(promiseHandle);
        Promise.resolve(promise).then(
            result => __coro_lib_awaiter_resolve(awaiterPtr, Emval.toHandle(result)),
            error => __coro_lib_awaiter_reject(awaiterPtr, Emval.toHandle(error))
        );
    },

    _coro_lib_sleep__deps: ['$Emval'],
    _coro_lib_sleep__sig: 'pi',
    _coro_lib_sleep: (time) => {
        const promise = new Promise(resolve => setTimeout(resolve, time));
        return Emval.toHandle(promise);
    },

});
