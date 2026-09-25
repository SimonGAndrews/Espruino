# XFSM

XFSM is the optional native state-machine engine selected by adding `XFSM` to
a board's `info.build.libraries` list. Espruino's board processing then sets
`USE_XFSM=1`, which includes this directory's wrapper and engine sources.

The current construction slice exposes `require("XFSM")` with working
`createMachine` and `assign` entry points. It compiles the M3 hierarchical
subset into the Version 1 native arena and retains only the JavaScript values
needed at runtime. `createActor` remains a controlled stub until the M4 actor
execution slice.

The normative behaviour and public API are defined by the
[Xstate-fsm-c Profile 1 specification](https://github.com/SimonGAndrews/XState-Espruino-Project/blob/main/projects/Xstate-fsm-c/docs/specification.md).

Build and exercise the shell on Linux with:

```bash
make clean
make USE_XFSM=1
bin/espruino --test libs/xfsm/tests/test_shell.js
bin/espruino --test libs/xfsm/tests/test_compile.js
bin/espruino --test libs/xfsm/tests/test_diagnostics.js
```

Run the portable native-format suite with address and undefined-behaviour
sanitizers:

```bash
make -C libs/xfsm/tests/native clean test
```
