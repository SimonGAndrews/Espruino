# XFSM

## Purpose

XFSM is an optional native hierarchical state-machine engine for Espruino. It
provides a compact, deterministic subset of XState-style statecharts for
embedded control applications while keeping the compiled machine structure
and execution coordinator in native C.

The initial supported feature set is called **Profile 1**. It covers machines
with one active state branch, including nested states, events, guards, actions,
context data, final states, and completion transitions. It is deliberately
smaller than the complete XState feature set: parallel states, delayed and
eventless transitions, invoked services, spawned actors, and history states
are not included.

Applications load the built-in module with `require("XFSM")`. The engine is
selected by adding `XFSM` to a board's `info.build.libraries` list, which makes
Espruino's board processing set `USE_XFSM=1` and include this directory's
wrapper and engine sources. The XFSM development workflow selects
`USE_XFSM=1` explicitly for continuous integration; it does not add XFSM to
any stock board definition.

## Current Implementation

The current implementation exposes `require("XFSM")` with working
`createMachine`, `assign`, and `createActor` entry points. `createMachine()`
validates the JavaScript definition and compiles it into a compact native
representation. `createActor()` creates an independent running instance with
its own current state, context, lifecycle, snapshots, and subscriptions;
multiple actors can share the same compiled machine.

The implemented behaviour includes nested state entry, hierarchical event
handling, guards, ordered actions and context assignments, wildcard events,
relative and ID-based transition targets, explicit state re-entry, final
states, completion transitions, stable snapshots, subscriptions, and
categorized configuration and runtime errors. Selected aliases are accepted
to ease migration from older XState machine definitions.

The detailed source of truth for supported syntax and behaviour is the
[Xstate-fsm-c Profile 1 specification](https://github.com/SimonGAndrews/XState-Espruino-Project/blob/main/projects/Xstate-fsm-c/docs/specification.md).

## How to Exercise XFSM

This section is for developers building or verifying the library. Run these
commands from the root of the Espruino repository. Readers using firmware that
already includes XFSM can continue to the [Quick Start](#quick-start).

The existing Espruino Linux build produces the host executable `bin/espruino`,
which includes a `--test` option. This option loads and runs the named
JavaScript file inside that Espruino interpreter, then returns a process exit
status suitable for automated testing. These commands therefore exercise the
native XFSM implementation through its JavaScript API without requiring a
physical board.

### Linux JavaScript Test Suite

Build an XFSM-enabled Linux interpreter and run the JavaScript test suite with:

```bash
make clean
make USE_XFSM=1
bin/espruino --test libs/xfsm/tests/test_shell.js
bin/espruino --test libs/xfsm/tests/test_compile.js
bin/espruino --test libs/xfsm/tests/test_diagnostics.js
bin/espruino --test libs/xfsm/tests/test_runtime.js
bin/espruino --test libs/xfsm/tests/test_completion.js
bin/espruino --test libs/xfsm/tests/test_completion_cascade.js
bin/espruino --test libs/xfsm/tests/test_targets.js
bin/espruino --test libs/xfsm/tests/test_events_migration.js
bin/espruino --test libs/xfsm/tests/test_profile1_diagnostics.js
bin/espruino --test libs/xfsm/tests/test_context_ownership.js
bin/espruino --test libs/xfsm/tests/test_assign_forms.js
bin/espruino --test libs/xfsm/tests/test_context_diagnostics.js
bin/espruino --test libs/xfsm/tests/test_runtime_errors.js
bin/espruino --test libs/xfsm/tests/test_subscriptions.js
```

### Resource Measurements

Flash, memory, stack, and execution-time measurements use a separate
instrumented build. The two private measurement methods in that build are
absent from normal firmware and are not part of the public API:

```bash
make clean
make USE_XFSM=1 XFC_MEASURE=1
bin/espruino --test libs/xfsm/tests/measure_m5.js
bin/espruino --test libs/xfsm/tests/measure_m5_completion.js
```

### Stack Reserve Test

When `start()`, `send()`, or `stop()` is called, XFSM enters its native
**execution coordinator**. The coordinator performs event lookup, evaluates
guards, runs actions, follows the state hierarchy, and processes any completion
transitions until the actor reaches a stable state.

The temporary C stack space used while that work is in progress is referred to
as the **XFSM coordinator frame**. It holds the operation's working data, such
as the current event, active and pending state indexes, hierarchy traversal,
action progress, and completion-step count. This memory exists only for the
duration of the synchronous call; it is separate from the compiled machine and
the actor data retained between calls.

Before beginning an operation, XFSM checks that enough C stack space remains
for this work. The following deliberately oversized build verifies that an
operation is rejected safely when the required headroom is unavailable:

```bash
make clean
make USE_XFSM=1 XFC_STACK_RESERVE=2000000
bin/espruino --test libs/xfsm/tests/test_stack_reserve.js
```

Normal builds require 768 bytes of available space on the processor's native C
call stack for the XFSM coordinator. This is the stack used while Espruino's
firmware C functions are executing, not the memory used to store JavaScript
variables or the machine's context. The requirement is in addition to
Espruino's 512-byte general C-stack safety allowance. If that space is
unavailable, the operation fails before changing the actor. A target may
override the private reserve at compile time by passing the optional Make
variable `XFC_STACK_RESERVE=<bytes>`. The Makefile converts this to the C
preprocessor definition `-DXFC_STACK_RESERVE=<bytes>` for the firmware build.
If the variable is omitted, XFSM uses the 768-byte default. This is a private
build setting rather than a JavaScript or runtime option, and it should be
changed only after measuring the target's actual maximum stack use.

### Native-Format Sanitizer Suite

When application JavaScript calls `createMachine()`, XFSM validates and
compiles the supplied JavaScript machine configuration at runtime. The result
is a compact internal C memory representation called the **native format**.
Actors execute this compiled representation without repeatedly reading the
original configuration object.

The native-format test suite checks this low-level C representation directly,
independently of the Espruino JavaScript interpreter. It uses the host C
compiler with AddressSanitizer and UndefinedBehaviorSanitizer enabled so that
invalid memory access, memory leaks, and undefined C operations cause the test
to fail. From the repository root, run:

```bash
make -C libs/xfsm/tests/native clean test
```

The `-C libs/xfsm/tests/native` argument tells Make to run in the native-test
directory. The `clean` target removes the previous test build, and the `test`
target rebuilds the C test executable and runs it.

## Quick Start

![QuickStart statechart](images/quick-start-statechart.png)

This example models a device with `idle` and `running` states. `START` moves it
to `running`, `STOP` returns it to `idle`, and each `PULSE` event increments a
counter without changing state. The `reportPulse` action runs after the counter
has been updated.

With XFSM enabled in the firmware, define the machine, create an actor from it,
and start the actor before sending events:

```javascript
var XFSM = require("XFSM");
var assign = XFSM.assign;

var machine = XFSM.createMachine({
  id: "QuickStart",
  context: { pulses: 0 },
  initial: "idle",
  states: {
    idle: {
      on: {
        START: "running"
      }
    },
    running: {
      on: {
        PULSE: {
          actions: [
            assign({
              pulses: function (context, event) {
                return context.pulses + event.amount;
              }
            }),
            "reportPulse"
          ]
        },
        STOP: "idle"
      }
    }
  }
}, {
  actions: {
    reportPulse: function (context, event) {
      print("pulse", event.amount, "total", context.pulses);
    }
  }
});

var actor = XFSM.createActor(machine);

var subscription = actor.subscribe(function (snapshot) {
  print(snapshot.status, JSON.stringify(snapshot.value),
        JSON.stringify(snapshot.context));
});

actor.start();
actor.send("START");
actor.send({ type: "PULSE", amount: 2 });

var snapshot = actor.getSnapshot();
print(snapshot.matches("running")); // true

actor.send("STOP");
actor.stop();
subscription.unsubscribe();
```

The machine definition describes its states and transitions. Its `context`
holds application data, while `assign()` produces an action that updates that
data. Named action and guard functions are supplied in the second argument to
`createMachine()`.

`createMachine()` validates and compiles the definition once. Each
`createActor()` call then creates an independent execution of that machine.
`start()`, `send()`, and `stop()` run synchronously. `getSnapshot()` reports
the actor's stable state and context, and subscribed listeners are called only
after an operation has completed successfully.
