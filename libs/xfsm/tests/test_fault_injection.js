echo(false);
(function () {
var XFSM = require("XFSM");

function capture(callback) {
  try { callback(); }
  catch (error) { return error; }
  return undefined;
}

function isMemoryError(error, path) {
  var message = "" + error;
  return error instanceof Error && !(error instanceof TypeError) &&
    message.indexOf("E_NO_MEMORY") >= 0 && message.indexOf(path) >= 0;
}

function simpleMachine(extra) {
  var config = {
    context: { count: 0 },
    initial: "Idle",
    states: { Idle: {}, Done: {} }
  };
  if (extra) extra(config);
  return XFSM.createMachine(config);
}

var checks = [];

var reusableConfig = { initial: "Idle", states: { Idle: {} } };
XFSM._failNext("compile.workspace");
var workspaceError = capture(function () {
  XFSM.createMachine(reusableConfig);
});
checks.push(isMemoryError(workspaceError, "createMachine") &&
            !!XFSM.createMachine(reusableConfig));

XFSM._failNext("compile.arena");
var compileError = capture(function () {
  XFSM.createMachine(reusableConfig);
});
checks.push(isMemoryError(compileError, "createMachine") &&
            !!XFSM.createMachine(reusableConfig));

var reusableMachine = XFSM.createMachine(reusableConfig);
XFSM._failNext("createActor");
var actorError = capture(function () { XFSM.createActor(reusableMachine); });
checks.push(isMemoryError(actorError, "createActor") &&
            !!XFSM.createActor(reusableMachine));

function startFailure(point, path) {
  var entries = 0;
  var machine = XFSM.createMachine({
    initial: "Idle",
    states: { Idle: { entry: "enter" } }
  }, { actions: { enter: function () { entries++; } } });
  var actor = XFSM.createActor(machine);
  XFSM._failNext(point);
  var observed = capture(function () { actor.start(); });
  var snapshot = actor.getSnapshot();
  return isMemoryError(observed, path) && snapshot.status === "error" &&
    snapshot.error === observed && snapshot.value === undefined &&
    snapshot.context === undefined && entries === 0;
}

checks.push(startFailure("start.context", "actor.start.context"));
checks.push(startFailure("start.event", "actor.start.event"));

var startNotices = 0;
var startEntries = 0;
var startNotifyMachine = XFSM.createMachine({
  initial: "Idle",
  states: { Idle: { entry: "enter" } }
}, { actions: { enter: function () { startEntries++; } } });
var startNotifyActor = XFSM.createActor(startNotifyMachine);
startNotifyActor.subscribe(function () { startNotices++; });
XFSM._failNext("notify.snapshot");
var startNotifyError = capture(function () { startNotifyActor.start(); });
var startNotifyFailed = startNotifyActor.getSnapshot();
checks.push(isMemoryError(startNotifyError, "actor.notify.snapshot") &&
  startNotifyFailed.status === "error" &&
  startNotifyFailed.error === startNotifyError &&
  startNotifyFailed.value === undefined &&
  startNotifyFailed.context === undefined && startEntries === 1 &&
  startNotices === 0);

var eventMachine = simpleMachine(function (config) {
  config.states.Idle.on = { GO: "Done" };
});
var eventActor = XFSM.createActor(eventMachine).start();
var eventStable = eventActor.getSnapshot();
XFSM._failNext("send.event");
var eventError = capture(function () { eventActor.send("GO"); });
var eventFailed = eventActor.getSnapshot();
checks.push(isMemoryError(eventError, "actor.send.event") &&
  eventFailed.status === "error" && eventFailed.error === eventError &&
  eventFailed.value === "Idle" && eventFailed.context === eventStable.context);

var assignMachine = XFSM.createMachine({
  context: { count: 0 },
  initial: "Idle",
  states: {
    Idle: {
      on: {
        GO: {
          target: "Done",
          actions: XFSM.assign({ count: function (context) {
            return context.count + 1;
          } })
        }
      }
    },
    Done: {}
  }
});
var assignActor = XFSM.createActor(assignMachine).start();
var assignStable = assignActor.getSnapshot();
XFSM._failNext("assign.context");
var assignError = capture(function () { assignActor.send("GO"); });
var assignFailed = assignActor.getSnapshot();
checks.push(isMemoryError(assignError, "actor.runtime.assign") &&
  assignFailed.status === "error" && assignFailed.error === assignError &&
  assignFailed.value === "Idle" && assignFailed.context === assignStable.context &&
  assignFailed.context.count === 0);

var completionMachine = XFSM.createMachine({
  initial: "Workflow",
  states: {
    Workflow: {
      initial: "Working",
      states: {
        Working: { on: { FINISH: "Completed" } },
        Completed: { type: "final" }
      },
      onDone: "Success"
    },
    Success: {}
  }
});
var completionActor = XFSM.createActor(completionMachine).start();
var completionStable = completionActor.getSnapshot();
XFSM._failNext("completion.event");
var completionError = capture(function () {
  completionActor.send("FINISH");
});
var completionFailed = completionActor.getSnapshot();
checks.push(isMemoryError(completionError, "actor.runtime.complete.event") &&
  completionFailed.status === "error" &&
  completionFailed.error === completionError &&
  completionFailed.matches({ Workflow: "Working" }) &&
  completionFailed.context === completionStable.context);

var notified = 0;
var notifyMachine = simpleMachine(function (config) {
  config.states.Idle.on = { GO: "Done" };
});
var notifyActor = XFSM.createActor(notifyMachine).start();
var notifyStable = notifyActor.getSnapshot();
notifyActor.subscribe(function () { notified++; });
XFSM._failNext("notify.snapshot");
var notifyError = capture(function () { notifyActor.send("GO"); });
var notifyFailed = notifyActor.getSnapshot();
checks.push(isMemoryError(notifyError, "actor.notify.snapshot") &&
  notifyFailed.status === "error" && notifyFailed.error === notifyError &&
  notifyFailed.value === "Idle" && notifyFailed.context === notifyStable.context &&
  notified === 0);

var snapshotActor = XFSM.createActor(reusableMachine).start();
XFSM._failNext("getSnapshot");
var snapshotError = capture(function () { snapshotActor.getSnapshot(); });
var recoveredSnapshot = snapshotActor.getSnapshot();
checks.push(isMemoryError(snapshotError, "actor.getSnapshot") &&
  recoveredSnapshot.status === "active" && recoveredSnapshot.value === "Idle");

var stopEntries = 0;
var stopExits = 0;
var stopMachine = XFSM.createMachine({
  initial: "Idle",
  states: { Idle: { entry: "enter", exit: "exit" } }
}, {
  actions: {
    enter: function () { stopEntries++; },
    exit: function () { stopExits++; }
  }
});
var stopActor = XFSM.createActor(stopMachine).start();
var stopStable = stopActor.getSnapshot();
XFSM._failNext("stop.event");
var stopError = capture(function () { stopActor.stop(); });
var stopFailed = stopActor.getSnapshot();
checks.push(isMemoryError(stopError, "actor.stop.event") &&
  stopFailed.status === "error" && stopFailed.error === stopError &&
  stopFailed.value === "Idle" && stopFailed.context === stopStable.context &&
  stopEntries === 1 && stopExits === 0);

var subscribeActor = XFSM.createActor(reusableMachine).start();
XFSM._failNext("subscribe");
var subscribeError = capture(function () {
  subscribeActor.subscribe(function () {});
});
var recoveredSubscription = subscribeActor.subscribe(function () {});
checks.push(isMemoryError(subscribeError, "actor.subscribe") &&
  subscribeActor.getSnapshot().status === "active" &&
  !!recoveredSubscription);
recoveredSubscription.unsubscribe();

var invalidPoint = capture(function () { XFSM._failNext("missing"); });
checks.push(invalidPoint instanceof TypeError);

result = checks.indexOf(false) < 0;
if (!result) print("CHECKS=" + checks.join(","));
print("TEST=xfsm_fault_injection");
print((result ? "PASS " : "FAIL ") + "fault_injection");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
