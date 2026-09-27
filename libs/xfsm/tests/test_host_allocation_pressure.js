echo(false);
(function () {
var XFSM = require("XFSM");
var action = { type: "noop" };
var transitionActions = [];
for (var actionIndex = 0; actionIndex < 1000; actionIndex++) {
  transitionActions.push(action);
}
var config = {
  initial: "Idle",
  states: {
    Idle: {
      on: {
        GO: { target: "Done", actions: transitionActions }
      }
    },
    Done: {}
  }
};
var options = {
  actions: { noop: function () {} }
};
var filler;
var beforeUsage = 0;
var pressureFree = -1;
var observedError;
var warmupErrorOk = false;
var settledUsage = 0;
var recoveredMachine;
var recoveredActor;
var recoveredUsage = 0;
var postFailureUsage = 0;
var pressureFailureOk = false;
var retryError;
var recoveryOk = false;
var passed = false;
var warmMachine;
var warmActor;

function isMemoryError(error) {
  return error instanceof Error &&
    ("" + error).indexOf("E_NO_MEMORY") >= 0;
}

function report(name, ok) {
  print((ok ? "PASS " : "FAIL ") + name);
  return ok;
}

print("TEST=xfsm_host_allocation_pressure");
warmMachine = XFSM.createMachine({
  initial: "Warm",
  states: { Warm: {} }
});
warmActor = XFSM.createActor(warmMachine).start();
warmActor.getSnapshot().matches("Warm");
warmActor = undefined;
warmMachine = undefined;
process.memory();
E.defrag();
process.memory();
beforeUsage = process.memory().usage;

try {
  filler = new Uint8Array(12000);
  try { XFSM.createMachine(config, options); }
  catch (error) { observedError = error; }
} catch (pressureError) {
  observedError = pressureError;
}
warmupErrorOk = isMemoryError(observedError);
filler = undefined;
observedError = undefined;
process.memory();
E.defrag();
process.memory();
settledUsage = process.memory().usage;

try {
  filler = new Uint8Array(12000);
  pressureFree = process.memory().free;
  try { XFSM.createMachine(config, options); }
  catch (error) { observedError = error; }
} catch (pressureError) {
  observedError = pressureError;
}

pressureFailureOk = isMemoryError(observedError);
filler = undefined;
observedError = undefined;
process.memory();
E.defrag();
process.memory();
postFailureUsage = process.memory().usage;

try {
  recoveredMachine = XFSM.createMachine(config, options);
  recoveredActor = XFSM.createActor(recoveredMachine).start();
} catch (error) {
  retryError = error;
}
if (retryError !== undefined) print("ERROR retry=" + retryError);
recoveryOk = retryError === undefined && recoveredActor &&
  recoveredActor.getSnapshot().matches("Idle");

recoveredActor = undefined;
recoveredMachine = undefined;
retryError = undefined;
process.memory();
recoveredUsage = process.memory().usage;

passed = true;
passed = report("warmup_allocation_failure", warmupErrorOk) && passed;
passed = report("production_allocation_failure", pressureFailureOk) && passed;
passed = report("failed_attempt_cleanup", postFailureUsage <= settledUsage) && passed;
passed = report("same_config_retry", recoveryOk) && passed;
passed = report("post_retry_cleanup", recoveredUsage <= postFailureUsage) && passed;
print("METRIC before_usage_blocks=" + beforeUsage);
print("METRIC settled_usage_blocks=" + settledUsage);
print("METRIC pressure_free_blocks=" + pressureFree);
print("METRIC post_failure_usage_blocks=" + postFailureUsage);
print("METRIC recovered_usage_blocks=" + recoveredUsage);
result = passed;
print("DONE=" + (passed ? "PASS" : "FAIL"));
})();
