/* XFC-CF-COMPAT-006: pinned XState 5.33.2 differential corpus. */
(function () {
  var XFSM = require("XFSM");

  XFCTrace.begin("XFC-CF-COMPAT-006",
    "shared Profile 1 and XState 5.33.2 statechart semantics",
    "differential");
  XFCTrace.context("reference", {
    engine: "xstate",
    version: "5.33.2",
    adaptations: [
      "callback argument shape",
      "assign callback syntax",
      "actor lifecycle API",
      "imports and module wrapper"
    ]
  });

  function action(name) {
    return function (context, event) {
      XFCTrace.action(name, context && context.count, event.type);
    };
  }

  var basicNotifications = [];
  var basic = XFSM.createActor(XFSM.createMachine({
    initial: { target: "Idle", actions: action("initial") },
    states: {
      Idle: {
        entry: action("enterIdle"),
        exit: action("exitIdle"),
        on: { GO: { target: "Done", actions: action("go") } }
      },
      Done: { entry: action("enterDone") }
    }
  }));
  basic.subscribe(function (snapshot) {
    basicNotifications.push(snapshot.value);
  });
  XFCTrace.call("start", null);
  basic.start();
  XFCTrace.snapshot("basic started", basic.getSnapshot());
  XFCTrace.call("send", { type: "GO" });
  basic.send({ type: "GO" });
  XFCTrace.snapshot("basic transitioned", basic.getSnapshot());
  XFCTrace.assert("committed subscription timing",
    basicNotifications.join(",") === "Idle,Done",
    basicNotifications, ["Idle", "Done"]);
  basic.stop();
  basic = undefined;
  basicNotifications = undefined;

  var selection = [];
  var selectionActor = XFSM.createActor(XFSM.createMachine({
    initial: "Parent",
    states: {
      Parent: {
        initial: "Child",
        states: {
          Child: {
            on: {
              GO: { guard: "never", actions: action("wrong") },
              BLOCK: [{ guard: "never", actions: action("wrong") }, {}],
              "*": { guard: "notFall", actions: "childWildcard" }
            }
          }
        },
        on: { FALL: { actions: "parentFall" } }
      }
    }
  }, {
    guards: {
      never: function (context, event) {
        selection.push("never:" + event.type);
        XFCTrace.context("guard", { name: "never", event: event.type, decision: false });
        return false;
      },
      notFall: function (context, event) {
        var decision = event.type !== "FALL";
        selection.push("notFall:" + event.type + ":" + decision);
        XFCTrace.context("guard", { name: "notFall", event: event.type, decision: decision });
        return decision;
      }
    },
    actions: {
      childWildcard: function (context, event) {
        selection.push("child:" + event.type);
        XFCTrace.action("childWildcard", undefined, event.type);
      },
      parentFall: function (context, event) {
        selection.push("parent:" + event.type);
        XFCTrace.action("parentFall", undefined, event.type);
      }
    }
  })).start();
  selectionActor.send({ type: "GO" });
  selectionActor.send({ type: "FALL" });
  selectionActor.send({ type: "BLOCK" });
  XFCTrace.assert("ordered guards wildcard and parent fallback",
    selection.join("|") ===
      "never:GO|notFall:GO:true|child:GO|notFall:FALL:false|parent:FALL|never:BLOCK",
    selection.join("|"),
    "never:GO|notFall:GO:true|child:GO|notFall:FALL:false|parent:FALL|never:BLOCK");
  selectionActor.stop();
  selectionActor = undefined;
  selection = undefined;

  var suppliedEvent = { type: "UPDATE", payload: 7 };
  var seenEvent;
  var ordered = [];
  var assignmentActor = XFSM.createActor(XFSM.createMachine({
    context: { count: 0 },
    initial: "Active",
    states: {
      Active: {
        on: {
          UPDATE: {
            actions: [
              function (context, event) {
                ordered.push("before:" + context.count);
                seenEvent = event;
              },
              XFSM.assign({
                count: function (context, event) {
                  return context.count + event.payload;
                }
              }),
              function (context, event) {
                ordered.push("after:" + context.count);
                seenEvent = seenEvent === event ? event : undefined;
              }
            ]
          }
        }
      }
    }
  })).start();
  assignmentActor.send(suppliedEvent);
  XFCTrace.context("assigned", { count: assignmentActor.getSnapshot().context.count });
  XFCTrace.assert("ordered assignment and exact object event",
    ordered.join("|") === "before:0|after:7" && seenEvent === suppliedEvent,
    ordered.join("|"), "before:0|after:7");
  assignmentActor.stop();
  assignmentActor = undefined;
  suppliedEvent = undefined;
  seenEvent = undefined;
  ordered = undefined;

  var factorySequence = 0;
  var factoryMachine = XFSM.createMachine({
    context: function () { return { count: ++factorySequence, nested: {} }; },
    initial: "Active",
    states: { Active: {} }
  });
  var factoryA = XFSM.createActor(factoryMachine).start();
  var factoryB = XFSM.createActor(factoryMachine).start();
  XFCTrace.assert("factory context isolation",
    factoryA.getSnapshot().context !== factoryB.getSnapshot().context &&
    factoryA.getSnapshot().context.nested !== factoryB.getSnapshot().context.nested &&
    factoryA.getSnapshot().context.count === 1 &&
    factoryB.getSnapshot().context.count === 2);
  factoryA.stop();
  factoryB.stop();
  factoryA = undefined;
  factoryB = undefined;
  factoryMachine = undefined;

  var selfTrace = [];
  function selfMark(name) {
    return function (context, event) {
      selfTrace.push(name);
      XFCTrace.action(name, undefined, event.type);
    };
  }
  var selfActor = XFSM.createActor(XFSM.createMachine({
    initial: "Active",
    states: {
      Active: {
        entry: selfMark("entry"),
        exit: selfMark("exit"),
        on: {
          TARGETLESS: { actions: selfMark("targetless") },
          PRESERVE: { target: "Active", actions: selfMark("preserve") },
          REENTER: { target: "Active", reenter: true, actions: selfMark("reenter") }
        }
      }
    }
  })).start();
  selfTrace = [];
  selfActor.send({ type: "TARGETLESS" });
  selfActor.send({ type: "PRESERVE" });
  selfActor.send({ type: "REENTER" });
  XFCTrace.assert("targetless preserved and re-entering self transitions",
    selfTrace.join("|") === "targetless|preserve|exit|reenter|entry",
    selfTrace.join("|"), "targetless|preserve|exit|reenter|entry");
  selfActor = undefined;
  selfTrace = undefined;

  var hierarchyTrace = [];
  function hierarchyMark(name) {
    return function (context, event) {
      hierarchyTrace.push(name);
      XFCTrace.action(name, undefined, event.type);
    };
  }
  var hierarchy = XFSM.createActor(XFSM.createMachine({
    id: "hierarchy",
    initial: "Parent",
    states: {
      Parent: {
        initial: "A",
        entry: hierarchyMark("enterParent"),
        exit: hierarchyMark("exitParent"),
        states: {
          A: {
            entry: hierarchyMark("enterA"),
            exit: hierarchyMark("exitA"),
            on: { NEXT: "B" }
          },
          B: {
            entry: hierarchyMark("enterB"),
            exit: hierarchyMark("exitB"),
            on: { OUT: "#hierarchy.Outside" }
          }
        },
        on: { RESET: ".A" }
      },
      Outside: { entry: hierarchyMark("enterOutside") }
    }
  })).start();
  hierarchyTrace = [];
  hierarchy.send({ type: "NEXT" });
  hierarchy.send({ type: "RESET" });
  hierarchy.send({ type: "NEXT" });
  hierarchy.send({ type: "OUT" });
  XFCTrace.snapshot("cross hierarchy", hierarchy.getSnapshot());
  XFCTrace.assert("relative ID and cross-hierarchy boundaries",
    hierarchyTrace.join("|") ===
      "exitA|enterB|exitB|enterA|exitA|enterB|exitB|exitParent|enterOutside",
    hierarchyTrace.join("|"),
    "exitA|enterB|exitB|enterA|exitA|enterB|exitB|exitParent|enterOutside");
  hierarchy.stop();
  hierarchy = undefined;
  hierarchyTrace = undefined;

  var completionTrace = [];
  function completionMark(name) {
    return function (context, event) {
      completionTrace.push(name + ":" + event.type);
      XFCTrace.action(name, undefined, event.type);
    };
  }
  var completion = XFSM.createActor(XFSM.createMachine({
    id: "completion",
    initial: "Workflow",
    states: {
      Workflow: {
        initial: "Working",
        exit: completionMark("exitWorkflow"),
        states: {
          Working: {
            exit: completionMark("exitWorking"),
            on: { FINISH: { target: "Completed", actions: completionMark("finish") } }
          },
          Completed: { type: "final", entry: completionMark("enterCompleted"), exit: completionMark("exitCompleted") }
        },
        onDone: { target: "Success", actions: completionMark("done") }
      },
      Success: { entry: completionMark("enterSuccess") }
    }
  })).start();
  completionTrace = [];
  completion.send({ type: "FINISH" });
  XFCTrace.snapshot("completion stable", completion.getSnapshot());
  XFCTrace.assert("final state completion ordering",
    completionTrace.join("|") === [
      "exitWorking:FINISH", "finish:FINISH", "enterCompleted:FINISH",
      "exitCompleted:xstate.done.state.completion.Workflow",
      "exitWorkflow:xstate.done.state.completion.Workflow",
      "done:xstate.done.state.completion.Workflow",
      "enterSuccess:xstate.done.state.completion.Workflow"
    ].join("|"), completionTrace);
  completion.stop();
  completion = undefined;
  completionTrace = undefined;

  var dotted = XFSM.createActor(XFSM.createMachine({
    initial: "Parent",
    states: {
      Parent: {
        initial: "Child",
        states: {
          Child: { on: { NEXT: "Grand" } },
          Grand: { initial: "Leaf", states: { Leaf: {} } }
        }
      }
    }
  })).start();
  dotted.send({ type: "NEXT" });
  var dottedSnapshot = dotted.getSnapshot();
  XFCTrace.snapshot("nested structural value", dottedSnapshot);
  XFCTrace.assert("structural snapshot matching",
    dottedSnapshot.matches("Parent") &&
    dottedSnapshot.matches({ Parent: "Grand" }) &&
    dottedSnapshot.matches({ Parent: { Grand: "Leaf" } }));
  dotted.stop();
  dotted = undefined;
  dottedSnapshot = undefined;

  var depthActions = 0;
  var depthLeaf = {
    id: "deepLeaf",
    entry: function () { depthActions++; },
    exit: function () { depthActions++; },
    on: { RESET: { target: "#deep.L1", actions: function () { depthActions++; } } }
  };
  var depth;
  for (depth = 31; depth >= 1; depth--) {
    var child = "L" + (depth + 1);
    var parent = {
      initial: child,
      entry: function () { depthActions++; },
      exit: function () { depthActions++; },
      states: {}
    };
    parent.states[child] = depthLeaf;
    depthLeaf = parent;
  }
  var depthStates = {};
  depthStates.L1 = depthLeaf;
  var deep = XFSM.createActor(XFSM.createMachine({
    id: "deep", initial: "L1", states: depthStates
  })).start();
  XFCTrace.assert("depth 32 startup actions", depthActions === 32, depthActions, 32);
  depthActions = 0;
  deep.send({ type: "RESET" });
  XFCTrace.assert("depth 32 least-common-ancestor action count",
    depthActions === 65, depthActions, 65);

  XFCTrace.finish();
})();
