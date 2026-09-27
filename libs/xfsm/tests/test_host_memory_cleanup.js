echo(false);
(function () {
var XFSM = require("XFSM");
var actorCount = 8;
var repeatCount = 6;
var totalBlocks = process.memory().total;
var actionCalls = 0;
var exitCalls = 0;
var pingCalls = 0;
var notifications = 0;
var fault = { source: "host_memory_cleanup" };
var increment = XFSM.assign({
  count: function (context, event) {
    return context.count + event.amount;
  }
});

var baseline;
var settledBaseline;
var liveUsage;
var liveFreeBlocks;
var sharedCleanupUsage;
var secondSharedCleanupUsage;
var repeatCleanupMaximum = 0;
var faultCleanupUsage;
var finalUsage;
var sharedRuntimeOk = true;
var secondSharedRuntimeOk = true;
var relocationOk = true;
var repeatRuntimeOk = true;
var faultRollbackOk = false;
var observedFault;
var stableSnapshot;
var failedSnapshot;
var machine;
var actor;
var subscription;
var actors;
var subscriptions;
var firstContext;
var index;
var snapshot;
var cycleUsage = 0;
var passed = true;
var warmError;

function usage() {
  return process.memory().usage;
}

function report(name, ok) {
  print((ok ? "PASS " : "FAIL ") + name);
  return ok;
}

function contextFactory() {
  return { count: 0 };
}

function allow(context, event) {
  return event.allow === true;
}

function record() {
  actionCalls++;
}

function recordExit() {
  exitCalls++;
}

function recordPing() {
  pingCalls++;
}

function observe() {
  notifications++;
}

function throwFault() {
  throw fault;
}

function makeMachine() {
  return XFSM.createMachine({
    context: contextFactory,
    initial: "Idle",
    states: {
      Idle: {
        on: {
          STEP: {
            target: "Active",
            guard: "allow",
            actions: [increment, "record"]
          }
        }
      },
      Active: {
        exit: "recordExit",
        on: {
          PING: { actions: "recordPing" }
        }
      }
    }
  }, {
    actions: {
      record: record,
      recordExit: recordExit,
      recordPing: recordPing
    },
    guards: { allow: allow }
  });
}

function makeFaultMachine() {
  return XFSM.createMachine({
    context: { retained: true },
    initial: "Idle",
    states: {
      Idle: { on: { FAIL: { target: "Done", actions: "fail" } } },
      Done: {}
    }
  }, {
    actions: { fail: throwFault }
  });
}

print("TEST=xfsm_host_memory_cleanup");

// Warm shared prototypes and one-time interpreter structures before measuring.
machine = makeMachine();
actor = XFSM.createActor(machine);
subscription = actor.subscribe(observe);
actor.start();
actor.send({ type: "STEP", allow: true, amount: 1 });
snapshot = actor.getSnapshot();
snapshot.matches("Active");
actor.send("PING");
actor.stop();
subscription.unsubscribe();
machine = undefined;
actor = undefined;
subscription = undefined;
snapshot = undefined;
process.memory();

machine = makeFaultMachine();
actor = XFSM.createActor(machine).start();
try { actor.send("FAIL"); }
catch (error) { warmError = error; }
snapshot = actor.getSnapshot();
machine = undefined;
actor = undefined;
snapshot = undefined;
warmError = undefined;
process.memory();

actionCalls = 0;
exitCalls = 0;
pingCalls = 0;
notifications = 0;
baseline = usage();

// Exercise several actors sharing one compiled machine and retained bindings.
machine = makeMachine();
actors = [];
subscriptions = [];
for (index = 0; index < actorCount; index++) {
  actor = XFSM.createActor(machine);
  subscription = actor.subscribe(observe);
  actor.start();
  actor.send({ type: "STEP", allow: true, amount: index + 1 });
  snapshot = actor.getSnapshot();
  sharedRuntimeOk = sharedRuntimeOk && snapshot.matches("Active") &&
    snapshot.context.count === index + 1 &&
    (firstContext === undefined || snapshot.context !== firstContext);
  if (firstContext === undefined) firstContext = snapshot.context;
  actors.push(actor);
  subscriptions.push(subscription);
}

process.memory();
for (index = 0; index < actorCount; index++) {
  actors[index].send("PING");
  relocationOk = relocationOk &&
    actors[index].getSnapshot().context.count === index + 1;
}
liveUsage = usage();
liveFreeBlocks = totalBlocks - liveUsage;

for (index = 0; index < actorCount; index++) {
  actors[index].stop();
  subscriptions[index].unsubscribe();
}
sharedRuntimeOk = sharedRuntimeOk &&
  actionCalls === actorCount && exitCalls === actorCount &&
  pingCalls === actorCount && notifications === actorCount * 4;

machine = undefined;
actor = undefined;
subscription = undefined;
actors = undefined;
subscriptions = undefined;
firstContext = undefined;
snapshot = undefined;
process.memory();
sharedCleanupUsage = usage();
settledBaseline = sharedCleanupUsage;

// Repeat the full shared graph after the harness and lazy paths are warm.
actionCalls = 0;
exitCalls = 0;
pingCalls = 0;
notifications = 0;
machine = makeMachine();
actors = [];
subscriptions = [];
for (index = 0; index < actorCount; index++) {
  actor = XFSM.createActor(machine);
  subscription = actor.subscribe(observe);
  actor.start();
  actor.send({ type: "STEP", allow: true, amount: index + 1 });
  actor.send("PING");
  secondSharedRuntimeOk = secondSharedRuntimeOk &&
    actor.getSnapshot().context.count === index + 1;
  actors.push(actor);
  subscriptions.push(subscription);
}
for (index = 0; index < actorCount; index++) {
  actors[index].stop();
  subscriptions[index].unsubscribe();
}
secondSharedRuntimeOk = secondSharedRuntimeOk &&
  actionCalls === actorCount && exitCalls === actorCount &&
  pingCalls === actorCount && notifications === actorCount * 4;
machine = undefined;
actor = undefined;
subscription = undefined;
actors = undefined;
subscriptions = undefined;
process.memory();
secondSharedCleanupUsage = usage();

// Repeated construction and destruction must not accumulate retained records.
for (index = 0; index < repeatCount; index++) {
  machine = makeMachine();
  actor = XFSM.createActor(machine);
  subscription = actor.subscribe(observe);
  actor.start();
  actor.send({ type: "STEP", allow: true, amount: index + 1 });
  repeatRuntimeOk = repeatRuntimeOk &&
    actor.getSnapshot().context.count === index + 1;
  actor.stop();
  subscription.unsubscribe();
  machine = undefined;
  actor = undefined;
  subscription = undefined;
  process.memory();
  cycleUsage = usage();
  if (cycleUsage > repeatCleanupMaximum)
    repeatCleanupMaximum = cycleUsage;
}

// A faulted actor and its retained exact error must also be reclaimable.
machine = makeFaultMachine();
actor = XFSM.createActor(machine).start();
stableSnapshot = actor.getSnapshot();
try { actor.send("FAIL"); }
catch (error) { observedFault = error; }
failedSnapshot = actor.getSnapshot();
faultRollbackOk = observedFault === fault &&
  failedSnapshot.status === "error" && failedSnapshot.error === fault &&
  failedSnapshot.matches("Idle") &&
  failedSnapshot.context === stableSnapshot.context;

machine = undefined;
actor = undefined;
stableSnapshot = undefined;
failedSnapshot = undefined;
observedFault = undefined;
process.memory();
faultCleanupUsage = usage();
finalUsage = faultCleanupUsage;

passed = report("shared_machine_runtime", sharedRuntimeOk) && passed;
passed = report("live_graph_gc_relocation", relocationOk) && passed;
passed = report("shared_graph_cleanup",
                secondSharedRuntimeOk &&
                secondSharedCleanupUsage <= settledBaseline) && passed;
passed = report("repeated_construction_runtime", repeatRuntimeOk) && passed;
passed = report("repeated_construction_cleanup",
                repeatCleanupMaximum <= settledBaseline) && passed;
passed = report("fault_rollback", faultRollbackOk) && passed;
passed = report("fault_graph_cleanup",
                faultCleanupUsage <= settledBaseline) && passed;
passed = report("final_memory_recovery",
                finalUsage <= settledBaseline) && passed;

print("METRIC pre_stress_usage_blocks=" + baseline);
print("METRIC settled_baseline_blocks=" + settledBaseline);
print("METRIC total_blocks=" + totalBlocks);
print("METRIC live_usage_blocks=" + liveUsage);
print("METRIC live_free_blocks=" + liveFreeBlocks);
print("METRIC shared_cleanup_usage_blocks=" + sharedCleanupUsage);
print("METRIC second_shared_cleanup_usage_blocks=" +
      secondSharedCleanupUsage);
print("METRIC repeat_cleanup_maximum_blocks=" + repeatCleanupMaximum);
print("METRIC fault_cleanup_usage_blocks=" + faultCleanupUsage);
print("METRIC final_usage_blocks=" + finalUsage);
print("METRIC actor_count=" + actorCount);
print("METRIC repeat_count=" + repeatCount);
result = passed;
print("DONE=" + (passed ? "PASS" : "FAIL"));
})();
