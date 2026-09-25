/* M5 Linux resource and timing harness. Requires XFC_MEASURE=1. */
var XFSM = require("XFSM");
var BLOCK_SIZE = process.memory().blocksize;
var ITERATIONS = 5000;

function median(values) {
  values.sort(function (a, b) { return a - b; });
  return values[(values.length / 2) | 0];
}

function timeSends(actor, event, count) {
  var trials = [];
  var trial;
  var i;
  var started;
  for (i = 0; i < 100; i++) actor.send(event);
  for (trial = 0; trial < 5; trial++) {
    started = getTime();
    for (i = 0; i < count; i++) actor.send(event);
    trials.push((getTime() - started) * 1000000 / count);
  }
  return median(trials);
}

function allocationCalibration() {
  var before = 0;
  var after = 0;
  before = XFSM._memoryUsage();
  after = XFSM._memoryUsage();
  return after - before;
}

function actorAllocation(machine, calibration) {
  var before = 0;
  var after = 0;
  var actor = null;
  before = XFSM._memoryUsage();
  actor = XFSM.createActor(machine);
  after = XFSM._memoryUsage();
  return { blocks: after - before - calibration, value: actor };
}

function snapshotAllocation(actor, calibration) {
  var before = 0;
  var after = 0;
  var snapshot = null;
  before = XFSM._memoryUsage();
  snapshot = actor.getSnapshot();
  after = XFSM._memoryUsage();
  return { blocks: after - before - calibration, value: snapshot };
}

function subscriptionAllocation(actor, listener, calibration) {
  var before = 0;
  var after = 0;
  var subscription = null;
  before = XFSM._memoryUsage();
  subscription = actor.subscribe(listener);
  after = XFSM._memoryUsage();
  return { blocks: after - before - calibration, value: subscription };
}

function makeDepthConfig(depth) {
  var node = {};
  var index;
  for (index = depth; index >= 1; index--) {
    var wrapper = {};
    if (index === depth) {
      wrapper = node;
    } else {
      wrapper.initial = "S" + (index + 1);
      wrapper.states = {};
      wrapper.states["S" + (index + 1)] = node;
    }
    if (index === 1) wrapper.on = { DEPTH: {} };
    node = wrapper;
  }
  var config = { initial: "S1", states: {} };
  config.states.S1 = node;
  return config;
}

function measureDepth(depth, calibration) {
  var machine;
  var actor;
  var snapshotCost;
  var afterStart;
  var afterSend;
  var elapsed;
  XFSM._measure(true);
  machine = XFSM.createMachine(makeDepthConfig(depth));
  actor = XFSM.createActor(machine);
  actor.start();
  afterStart = XFSM._measure(false);
  snapshotCost = snapshotAllocation(actor, calibration);
  elapsed = timeSends(actor, "DEPTH", ITERATIONS);
  actor.send("DEPTH");
  afterSend = XFSM._measure(false);
  return {
    depth: depth,
    arenaBytes: machine["\xFFxfcA"].length,
    constructionPeakBlocks: afterStart.constructionPeakBlocks,
    startStackBytes: afterStart.lastStackBytes,
    firstSnapshotBlocks: snapshotCost.blocks,
    parentFallbackMicroseconds: elapsed,
    sendStackBytes: afterSend.lastStackBytes,
    maximumStackBytes: afterSend.maximumStackBytes
  };
}

var noop = function () {};
var allow = function () { return true; };
var config = {
  context: { count: 0 },
  initial: "Parent",
  states: {
    Parent: {
      initial: "Leaf",
      on: { PARENT: {} },
      states: {
        Leaf: {
          on: {
            LOCAL: {},
            GUARDED: { guard: "allow" }
          }
        }
      }
    }
  }
};
var options = { actions: { noop: noop }, guards: { allow: allow } };
var calibration = allocationCalibration();
var machineBefore = 0;
var machineAfter = 0;
var machine;
var machineMetrics;
var actorCost;
var snapshotCost;
var subscriptionCost;
var actor;
var listener = function () {};

XFSM._measure(true);
machineBefore = XFSM._memoryUsage();
machine = XFSM.createMachine(config, options);
machineAfter = XFSM._memoryUsage();
machineMetrics = XFSM._measure(false);
actorCost = actorAllocation(machine, calibration);
actor = actorCost.value;
snapshotCost = snapshotAllocation(actor, calibration);
subscriptionCost = subscriptionAllocation(actor, listener, calibration);
subscriptionCost.value.unsubscribe();
actor.start();

var dispatch = {
  iterations: ITERATIONS,
  localHitMicroseconds: timeSends(actor, "LOCAL", ITERATIONS),
  parentFallbackMicroseconds: timeSends(actor, "PARENT", ITERATIONS),
  guardedMicroseconds: timeSends(actor, "GUARDED", ITERATIONS),
  unhandledMicroseconds: timeSends(actor, "MISSING", ITERATIONS)
};

XFSM._measure(true);
var diagnosticText = "";
try {
  XFSM.createMachine({
    initial: "Parent",
    states: {
      Parent: {
        initial: "Child",
        states: {
          Child: { on: { GO: { target: "Missing" } } }
        }
      }
    }
  });
} catch (error) {
  diagnosticText = String(error);
}
var diagnosticMetrics = XFSM._measure(false);

var relocationOk = false;
var relocationMachine = XFSM.createMachine(config, options);
var relocationActor = XFSM.createActor(relocationMachine).start();
process.memory();
E.defrag();
relocationActor.send("LOCAL");
relocationOk = relocationActor.getSnapshot().matches({ Parent: "Leaf" });

var report = {
  blockSize: BLOCK_SIZE,
  allocationCalibrationBlocks: calibration,
  representative: {
    arenaBytes: machine["\xFFxfcA"].length,
    retainedValues: machine["\xFFxfcR"].length,
    machinePersistentBlocks: machineAfter - machineBefore - calibration,
    constructionPeakBlocks: machineMetrics.constructionPeakBlocks,
    actorBlocks: actorCost.blocks,
    firstSnapshotBlocks: snapshotCost.blocks,
    firstSubscriptionBlocks: subscriptionCost.blocks
  },
  dispatch: dispatch,
  hierarchy: [
    measureDepth(1, calibration),
    measureDepth(8, calibration),
    measureDepth(16, calibration),
    measureDepth(32, calibration)
  ],
  diagnostics: {
    text: diagnosticText,
    characters: diagnosticText.length,
    peakBlocks: diagnosticMetrics.diagnosticPeakBlocks,
    endBlocks: diagnosticMetrics.constructionEndBlocks
  },
  gcRelocation: {
    garbageCollectionCompleted: true,
    defragmentationCompleted: true,
    dispatchAndSnapshotValid: relocationOk
  }
};

print("M5_RESULT=" + JSON.stringify(report));
result = relocationOk &&
  report.representative.arenaBytes > 0 &&
  report.representative.actorBlocks > 0 &&
  report.hierarchy.length === 4 &&
  report.diagnostics.characters > 0;
