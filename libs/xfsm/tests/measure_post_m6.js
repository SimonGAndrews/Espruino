/* Post-M6 whole-engine resource harness. Requires XFC_MEASURE=1. */
echo(false);
print("TEST=xfsm_post_m6_resource");
(function () {
var XFSM = require("XFSM");

function memorySummary() {
  var memory = process.memory();
  return {
    usage: memory.usage,
    free: memory.free,
    total: memory.total,
    blockSize: memory.blocksize
  };
}

function heapSummary() {
  if (typeof ESP32 === "undefined") return undefined;
  var state = ESP32.getState();
  return {
    freeHeap: state.freeHeap,
    minimumFreeHeap: state.minHeap,
    largestFreeBlock: state.largestBlock
  };
}

function makeFixture() {
  var increment = XFSM.assign({
    count: function (context) { return context.count + 1; },
    last: function (context, event) { return event.type; }
  });
  var finish = XFSM.assign(function (context) {
    return { count: context.count, last: "complete", payload: context.payload };
  });
  var config = {
    id: "resourceMachine",
    context: function () {
      return { count: 0, last: "", payload: { retained: true } };
    },
    initial: "Controller",
    states: {
      Controller: {
        id: "controller",
        entry: "record",
        exit: "record",
        initial: "Idle",
        states: {
          Idle: {
            entry: "record",
            on: {
              START: {
                target: "Running",
                guard: "allowed",
                actions: ["record", increment]
              },
              PING: { actions: "record" },
              "*": {}
            }
          },
          Running: {
            initial: { target: "PhaseA", actions: "record" },
            states: {
              PhaseA: { on: { NEXT: "PhaseB" } },
              PhaseB: { on: { FINISH: "Completed" } },
              Completed: { type: "final" }
            },
            onDone: { target: "#resourceMachine.Done", actions: finish }
          }
        }
      },
      Done: { type: "final" }
    }
  };
  var options = {
    actions: { record: function () {} },
    guards: { allowed: function () { return true; } }
  };
  return { config: config, options: options };
}

var initialMemory = memorySummary();
var initialHeap = heapSummary();
var fixture = makeFixture();
var calibrationBefore = XFSM._memoryUsage();
var calibration = XFSM._memoryUsage() - calibrationBefore;
var machineBefore = XFSM._memoryUsage();

XFSM._measure(true);
var machine = XFSM.createMachine(fixture.config, fixture.options);
var constructionMetrics = XFSM._measure(false);
var machineAfter = XFSM._memoryUsage();

var actorBefore = XFSM._memoryUsage();
var actor = XFSM.createActor(machine);
var actorAfter = XFSM._memoryUsage();

XFSM._measure(true, true);
var startBefore = XFSM._memoryUsage();
actor.start();
var startAfter = XFSM._memoryUsage();
var startMetrics = XFSM._measure(false);

var snapshotBefore = XFSM._memoryUsage();
var snapshot = actor.getSnapshot();
var snapshotAfter = XFSM._memoryUsage();

var subscriptionBefore = XFSM._memoryUsage();
var subscription = actor.subscribe(function () {});
var subscriptionAfter = XFSM._memoryUsage();

XFSM._measure(true, true);
var sendBefore = XFSM._memoryUsage();
actor.send({ type: "START", source: "resource" });
var sendAfter = XFSM._memoryUsage();
var sendMetrics = XFSM._measure(false);
var active = actor.getSnapshot();

var resource = {
  initialMemory: initialMemory,
  initialHeap: initialHeap,
  calibrationBlocks: calibration,
  machine: {
    arenaBytes: machine["\xFFxfcA"].length,
    retainedValues: machine["\xFFxfcR"].length,
    persistentBlocks: machineAfter - machineBefore - calibration,
    constructionPeakBlocks: constructionMetrics.constructionPeakBlocks,
    constructionEndBlocks: constructionMetrics.constructionEndBlocks
  },
  actor: {
    beforeStartBlocks: actorAfter - actorBefore - calibration,
    startPersistentBlocks: startAfter - startBefore - calibration,
    startSampledPeakBlocks: startMetrics.operationPeakBlocks,
    startEndBlocks: startMetrics.operationEndBlocks,
    startStackBytes: startMetrics.lastStackBytes
  },
  snapshot: {
    firstMaterializationBlocks: snapshotAfter - snapshotBefore - calibration
  },
  subscription: {
    firstRegistrationBlocks: subscriptionAfter - subscriptionBefore - calibration
  },
  send: {
    persistentDeltaBlocks: sendAfter - sendBefore - calibration,
    sampledPeakBlocks: sendMetrics.operationPeakBlocks,
    endBlocks: sendMetrics.operationEndBlocks,
    stackBytes: sendMetrics.lastStackBytes,
    state: active.value,
    contextCount: active.context.count
  },
  finalHeap: heapSummary()
};

var valid = resource.machine.arenaBytes > 0 &&
  resource.machine.retainedValues > 0 &&
  resource.machine.persistentBlocks > 0 &&
  resource.actor.beforeStartBlocks > 0 &&
  resource.snapshot.firstMaterializationBlocks > 0 &&
  resource.subscription.firstRegistrationBlocks > 0 &&
  resource.send.sampledPeakBlocks > 0 &&
  active.matches({ Controller: { Running: "PhaseA" } }) &&
  active.context.count === 1;

print("POST_M6_RESOURCE=" + JSON.stringify(resource));
print("METRIC arena_bytes=" + resource.machine.arenaBytes);
print("METRIC construction_peak_blocks=" +
      resource.machine.constructionPeakBlocks);
print("METRIC send_peak_blocks=" + resource.send.sampledPeakBlocks);
print("METRIC send_stack_bytes=" + resource.send.stackBytes);

subscription.unsubscribe();
actor.stop();
active = undefined;
snapshot = undefined;
subscription = undefined;
actor = undefined;
machine = undefined;
fixture = undefined;
process.memory();

result = valid;
print((result ? "PASS " : "FAIL ") + "post_m6_resource");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
