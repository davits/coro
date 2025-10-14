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
    _coro_lib_await_promise__sig: 'ppp',
    _coro_lib_await_promise: (promiseHandle, awaiterPtr) => {
        const promise = Emval.toValue(promiseHandle);
        const controller = new AbortController()
        const signal = controller.signal
        Promise.resolve(promise).then(
            result => {
                if (!signal.aborted) {
                    __coro_lib_awaiter_resolve(awaiterPtr, Emval.toHandle(result));
                }
            },
            error => {
                if (!signal.aborted) {
                    __coro_lib_awaiter_reject(awaiterPtr, Emval.toHandle(error));
                }
            }
        );
        return Emval.toHandle(controller);
    },

    _coro_lib_sleep__deps: ['$Emval'],
    _coro_lib_sleep__sig: 'pi',
    _coro_lib_sleep: (time) => {
        const obj = {
            id: null,
            cancel() {
                if (this.id) {
                    clearTimeout(this.id);
                    this.id = null;
                }
            },
        };
        obj.promise = new Promise(resolve => {
            obj.id = setTimeout(() => {
                resolve();
                obj.id = null;
            }, time);
        });
        return Emval.toHandle(obj);
    },

    _coro_lib_val_from_cpp_exception__deps: ['$Emval', '__cxa_rethrow'],
    _coro_lib_val_from_cpp_exception__sig: 'p',
    _coro_lib_val_from_cpp_exception: () => {
        try {
            // __cxa_rethrow does conversion to JS Error
            ___cxa_rethrow();
        } catch (e) {
            return Emval.toHandle(e);
        }
    },

});
