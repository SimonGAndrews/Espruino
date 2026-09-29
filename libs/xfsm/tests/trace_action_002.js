/* XFC-CF-ACTION-002: action/guard forms and callback contract. */
(function () {
  var XFSM = require("XFSM");
  var records = [];
  var initEvent;
  var goEvent;
  var stopEvent;
  var receiverChecks = [];
  var argumentChecks = [];

  function mark(name, returned) {
    return function (context, event) {
      "use strict";
      records.push(name + ":" + event.type + ":" + context.count);
      receiverChecks.push(this === global);
      argumentChecks.push(arguments.length === 2);
      if (event.type === "xstate.init") {
        if (!initEvent) initEvent = event;
        argumentChecks.push(event === initEvent);
      }
      if (event.type === "GO") argumentChecks.push(event === goEvent);
      if (event.type === "xstate.stop") stopEvent = event;
      return returned;
    };
  }

  function retainedOccurrences(machine, value) {
    var retained = machine["\xFFxfcR"];
    var count = 0;
    for (var index = 0; index < retained.length; index++)
      if (retained[index] === value) count++;
    return count;
  }

  function rejects(name, operation, category, path) {
    var caught;
    try { operation(); } catch (error) { caught = error; }
    if (caught) XFCTrace.error(name, category, path, caught);
    XFCTrace.assert(name, caught instanceof Error &&
      ("" + caught).indexOf("XFC " + category + " @ " + path) >= 0);
  }

  XFCTrace.begin("XFC-CF-ACTION-002",
    "action forms, callback arguments, receiver and event contract",
    "profile-internal");

  var named = mark("named");
  var boundReceiver = { name: "bound" };
  var boundSeen = false;
  function boundTarget(context, event) {
    boundSeen = this === boundReceiver;
    records.push("bound:" + event.type + ":" + context.count);
  }
  var bound = boundTarget.bind(boundReceiver);
  var thenObserved = false;
  var ignoredThenable = { then: function () { thenObserved = true; } };
  var machine = XFSM.createMachine({
    context: { count: 0 },
    entry: [mark("rootEntry"), "named", { type: "named" }],
    exit: mark("rootExit"),
    initial: {
      target: "Active",
      actions: [mark("initialDirect"), "named", { type: "named" },
        XFSM.assign({ count: 1 })]
    },
    states: {
      Active: {
        entry: [mark("activeEntry"), bound],
        exit: mark("activeExit"),
        on: {
          GO: {
            target: "Done",
            actions: [
              mark("returnIgnored", { count: 99 }),
              "asyncIgnored",
              { type: "named" },
              XFSM.assign({ count: function (context) { return context.count + 1; } }),
              mark("afterAssign")
            ]
          }
        }
      },
      Done: { type: "final", entry: mark("doneEntry"), exit: mark("doneExit") }
    }
  }, {
    actions: {
      named: named,
      asyncIgnored: mark("asyncIgnored", ignoredThenable)
    }
  });
  XFCTrace.assert("repeated named implementation retained once",
    retainedOccurrences(machine, named) === 1);
  var actor = XFSM.createActor(machine).start();
  XFCTrace.assert("startup forms and ordered context visibility",
    records.join(",") === [
      "rootEntry:xstate.init:0", "named:xstate.init:0",
      "named:xstate.init:0", "initialDirect:xstate.init:0",
      "named:xstate.init:0", "named:xstate.init:0",
      "activeEntry:xstate.init:1", "bound:xstate.init:1"
    ].join(",") && actor.getSnapshot().context.count === 1 && boundSeen);
  goEvent = { type: "GO", payload: 9 };
  actor.send(goEvent);
  XFCTrace.context("action observations", {
    tail: records.slice(-10),
    receivers: receiverChecks,
    arguments: argumentChecks,
    thenObserved: thenObserved
  });
  XFCTrace.assert("transition forms, return ignoring and exact event",
    records.slice(-8).join(",") === [
      "activeExit:GO:1", "returnIgnored:GO:1", "asyncIgnored:GO:1",
      "named:GO:1", "afterAssign:GO:2", "doneEntry:GO:2",
      "doneExit:GO:2", "rootExit:GO:2"
    ].join(",") && actor.getSnapshot().context.count === 2 && !thenObserved);
  XFCTrace.assert("unbound callbacks use Espruino receiver and two arguments",
    receiverChecks.every(function (value) { return value; }) &&
    argumentChecks.every(function (value) { return value; }));

  var stopRecords = [];
  var stopActor = XFSM.createActor(XFSM.createMachine({
    initial: "Ready",
    states: {
      Ready: {
        exit: function (context, event) {
          stopRecords.push(event.type);
          stopEvent = event;
        }
      }
    }
  })).start();
  stopActor.stop();
  XFCTrace.assert("stop exits receive stop event",
    stopRecords.join(",") === "xstate.stop" && stopEvent.type === "xstate.stop");

  var completionTypes = [];
  var completionActor = XFSM.createActor(XFSM.createMachine({
    id: "completion-actions",
    initial: "Work",
    states: {
      Work: {
        initial: "Running",
        states: {
          Running: { on: { FINISH: "Complete" } },
          Complete: { type: "final", entry: function (context, event) {
            completionTypes.push("final:" + event.type);
          } }
        },
        onDone: { target: "Success", actions: function (context, event) {
          completionTypes.push("done:" + event.type);
        } }
      },
      Success: {}
    }
  })).start();
  completionActor.send({ type: "FINISH" });
  XFCTrace.assert("completion action receives generated event",
    completionTypes[0] === "final:FINISH" &&
    completionTypes[1] === "done:xstate.done.state.completion-actions.Work");

  actor = undefined;
  machine = undefined;
  stopActor = undefined;
  completionActor.stop();
  completionActor = undefined;
  records = undefined;
  receiverChecks = undefined;
  argumentChecks = undefined;
  completionTypes = undefined;
  if (typeof E !== "undefined" && E.gc) E.gc();

  rejects("unresolved action rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { entry: "missing" } } });
  }, "E_ACTION_UNRESOLVED", "config.states.A.entry");
  rejects("action params rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { entry: { type: "work", params: {} } } } },
      { actions: { work: function () {} } });
  }, "E_UNKNOWN_PROPERTY", "config.states.A.entry.params");
  rejects("action exec rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { entry: { type: "work", exec: function () {} } } } },
      { actions: { work: function () {} } });
  }, "E_UNKNOWN_PROPERTY", "config.states.A.entry.exec");
  rejects("malformed action rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { entry: 1 } } });
  }, "E_CONFIG_TYPE", "config.states.A.entry");

  XFCTrace.finish();
})();
