echo(false);
(function () {
var XFSM = require("XFSM");
var passed = true;
var checks = 0;

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

function report(name, callback) {
  var ok = false;
  try { ok = callback() === true; }
  catch (error) { print("INFO " + name + "=" + error); }
  checks++;
  passed = passed && ok;
  print((ok ? "PASS " : "FAIL ") + name);
  process.memory();
}

function simpleMachine() {
  return XFSM.createMachine({
    initial: "Idle",
    states: { Idle: {}, Done: {} }
  });
}

print("TEST=xfsm_fault_injection_embedded");

report("compile_workspace", function () {
  var config = { initial: "Idle", states: { Idle: {} } };
  XFSM._failNext("compile.workspace");
  var error = capture(function () { XFSM.createMachine(config); });
  return isMemoryError(error, "createMachine") &&
    !!XFSM.createMachine(config);
});

report("compile_arena", function () {
  var config = { initial: "Idle", states: { Idle: {} } };
  XFSM._failNext("compile.arena");
  var error = capture(function () { XFSM.createMachine(config); });
  return isMemoryError(error, "createMachine") &&
    !!XFSM.createMachine(config);
});

report("create_actor", function () {
  var machine = simpleMachine();
  XFSM._failNext("createActor");
  var error = capture(function () { XFSM.createActor(machine); });
  return isMemoryError(error, "createActor") &&
    !!XFSM.createActor(machine);
});

report("start_context", function () {
  var entries = 0;
  var machine = XFSM.createMachine({
    initial: "Idle",
    states: { Idle: { entry: "enter" } }
  }, { actions: { enter: function () { entries++; } } });
  var actor = XFSM.createActor(machine);
  XFSM._failNext("start.context");
  var error = capture(function () { actor.start(); });
  var snapshot = actor.getSnapshot();
  return isMemoryError(error, "actor.start.context") &&
    snapshot.status === "error" && snapshot.error === error &&
    snapshot.value === undefined && snapshot.context === undefined &&
    entries === 0;
});

report("assign_context", function () {
  var machine = XFSM.createMachine({
    context: { count: 0 },
    initial: "Idle",
    states: {
      Idle: {
        on: {
          GO: {
            target: "Done",
            actions: XFSM.assign({
              count: function (context) { return context.count + 1; }
            })
          }
        }
      },
      Done: {}
    }
  });
  var actor = XFSM.createActor(machine).start();
  var stable = actor.getSnapshot();
  XFSM._failNext("assign.context");
  var error = capture(function () { actor.send("GO"); });
  var failed = actor.getSnapshot();
  return isMemoryError(error, "actor.runtime.assign") &&
    failed.status === "error" && failed.error === error &&
    failed.value === "Idle" && failed.context === stable.context &&
    failed.context.count === 0;
});

report("notification_snapshot", function () {
  var notices = 0;
  var machine = XFSM.createMachine({
    initial: "Idle",
    states: {
      Idle: { on: { GO: "Done" } },
      Done: {}
    }
  });
  var actor = XFSM.createActor(machine).start();
  var stable = actor.getSnapshot();
  actor.subscribe(function () { notices++; });
  XFSM._failNext("notify.snapshot");
  var error = capture(function () { actor.send("GO"); });
  var failed = actor.getSnapshot();
  return isMemoryError(error, "actor.notify.snapshot") &&
    failed.status === "error" && failed.error === error &&
    failed.value === "Idle" && failed.context === stable.context &&
    notices === 0;
});

report("snapshot_materialization", function () {
  var actor = XFSM.createActor(simpleMachine()).start();
  XFSM._failNext("getSnapshot");
  var error = capture(function () { actor.getSnapshot(); });
  var recovered = actor.getSnapshot();
  return isMemoryError(error, "actor.getSnapshot") &&
    recovered.status === "active" && recovered.value === "Idle";
});

report("subscription_node", function () {
  var actor = XFSM.createActor(simpleMachine()).start();
  XFSM._failNext("subscribe");
  var error = capture(function () {
    actor.subscribe(function () {});
  });
  var recovered = actor.subscribe(function () {});
  var ok = isMemoryError(error, "actor.subscribe") && !!recovered &&
    actor.getSnapshot().status === "active";
  recovered.unsubscribe();
  return ok;
});

print("METRIC checked_faults=" + checks);
result = passed;
print("DONE=" + (passed ? "PASS" : "FAIL"));
})();
