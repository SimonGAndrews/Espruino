# XFSM

XFSM is the optional native state-machine engine selected by adding `XFSM` to
a board's `info.build.libraries` list. Espruino's board processing then sets
`USE_XFSM=1`, which includes this directory's wrapper and engine sources.

The current vertical slice exposes `require("XFSM")` with working
`createMachine`, `assign`, and `createActor` entry points. It compiles the M3
hierarchical subset into the Version 1 native arena and executes that native
structure through the M4 actor lifecycle, dispatch, action, assignment,
snapshot, and subscription paths.

The normative behaviour and public API are defined by the
[Xstate-fsm-c Profile 1 specification](https://github.com/SimonGAndrews/XState-Espruino-Project/blob/main/projects/Xstate-fsm-c/docs/specification.md).

Build and exercise the shell on Linux with:

```bash
make clean
make USE_XFSM=1
bin/espruino --test libs/xfsm/tests/test_shell.js
bin/espruino --test libs/xfsm/tests/test_compile.js
bin/espruino --test libs/xfsm/tests/test_diagnostics.js
bin/espruino --test libs/xfsm/tests/test_runtime.js
bin/espruino --test libs/xfsm/tests/test_runtime_errors.js
bin/espruino --test libs/xfsm/tests/test_subscriptions.js
```

M5 resource measurements use a separate instrumented build. The two private
methods in that build are absent from normal firmware and are not public API:

```bash
make clean
make USE_XFSM=1 XFC_MEASURE=1
bin/espruino --test libs/xfsm/tests/measure_m5.js
```

The stack-reserve rejection path has a separate deliberately oversized build:

```bash
make clean
make USE_XFSM=1 XFC_STACK_RESERVE=2000000
bin/espruino --test libs/xfsm/tests/test_stack_reserve.js
```

Normal builds reserve 512 bytes for one XFSM coordinator frame in addition to
Espruino's 512-byte stack safety allowance. Targets may override the private
reserve with `XFC_STACK_RESERVE=<bytes>` after target-specific measurement.

Run the portable native-format suite with address and undefined-behaviour
sanitizers:

```bash
make -C libs/xfsm/tests/native clean test
```
