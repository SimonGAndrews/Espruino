echo(false);
(function () {
var Storage = require("Storage");
var fileName = "xfc_d32";
var source = `(function () {
var XFSM = require("XFSM");
var trace = [];
var beforeMemory = process.memory();
var beforeUsage = beforeMemory.usage;
var totalBlocks = beforeMemory.total;
var leaf = {
  id: "deepLeaf",
  entry: "enter",
  exit: "exit",
  on: { RESET: { target: "#deep.L1", actions: "reset" } }
};

for (var depth = 31; depth >= 1; depth--) {
  var child = "L" + (depth + 1);
  var parent = {
    initial: child,
    entry: "enter",
    exit: "exit",
    states: {}
  };
  parent.states[child] = leaf;
  leaf = parent;
}

var configuredUsage = process.memory().usage;
var machine = XFSM.createMachine({
  id: "deep",
  initial: "L1",
  states: { L1: leaf }
}, {
  actions: {
    enter: function () { trace.push("enter"); },
    exit: function () { trace.push("exit"); },
    reset: function () { trace.push("reset"); }
  }
});
var compiledUsage = process.memory().usage;
var actor = XFSM.createActor(machine).start();
var startupOk = trace.length === 32;
for (var index = 0; startupOk && index < 32; index++)
  startupOk = trace[index] === "enter";

trace = [];
actor.send("RESET");
var transitionOk = trace.length === 65 && trace[32] === "reset";
for (var action = 0; transitionOk && action < 32; action++)
  transitionOk = trace[action] === "exit" &&
    trace[action + 33] === "enter";

var activeOk = actor.getSnapshot().matches("L1");
actor = undefined;
machine = undefined;
leaf = undefined;
parent = undefined;
trace = undefined;
XFSM = undefined;
process.memory();
return {
  ok: startupOk && transitionOk && activeOk,
  totalBlocks: totalBlocks,
  beforeUsage: beforeUsage,
  configuredUsage: configuredUsage,
  compiledUsage: compiledUsage,
  finalUsage: process.memory().usage
};
})()`;

print("TEST=xfsm_transition_depth_storage");
Storage.erase(fileName);
Storage.write(fileName, source);
source = undefined;

setTimeout(function () {
  var outcome;
  var observedError;
  try { outcome = eval(Storage.read(fileName)); }
  catch (error) { observedError = error; }
  process.memory();
  Storage.erase(fileName);

  var executionOk = observedError === undefined && outcome && outcome.ok;
  var cleanupOk = Storage.read(fileName) === undefined;
  print((executionOk ? "PASS " : "FAIL ") +
        "flash_backed_depth32_actions");
  print((cleanupOk ? "PASS " : "FAIL ") + "storage_cleanup");
  if (outcome) {
    print("METRIC total_blocks=" + outcome.totalBlocks);
    print("METRIC before_usage_blocks=" + outcome.beforeUsage);
    print("METRIC configured_usage_blocks=" + outcome.configuredUsage);
    print("METRIC compiled_usage_blocks=" + outcome.compiledUsage);
    print("METRIC final_usage_blocks=" + outcome.finalUsage);
  }
  if (typeof ESP32 !== "undefined") {
    var espState = ESP32.getState();
    print("METRIC native_free_heap_bytes=" + espState.freeHeap);
    print("METRIC native_min_heap_bytes=" + espState.minHeap);
    print("METRIC native_largest_block_bytes=" + espState.largestBlock);
  } else {
    print("INFO native_heap_metrics=not_available_on_target");
  }
  if (observedError !== undefined) print("ERROR=" + observedError);
  result = executionOk && cleanupOk;
  print("DONE=" + (result ? "PASS" : "FAIL"));
}, 25);
})();
