echo(false);
(function () {
var XFSM = require("XFSM");
var baselineUsage = 0;
var finalUsage = 0;
var prePressureFree = 0;
var pressureFree = 0;
var longEvent;
var on;
var config;
var fallbackOk = false;
var warmFallbackOk = false;
var cleanupOk = false;
var passed = false;
var settledUsage = 0;
var postFailureUsage = 0;
var fillerBytes = 0;

function isFallbackError(error) {
  return error instanceof Error &&
    ("" + error).indexOf("XFC E_NO_MEMORY @ createMachine") >= 0;
}

function report(name, ok) {
  print((ok ? "PASS " : "FAIL ") + name);
  return ok;
}

function attempt(measure) {
  var localError;
  var memory = process.memory();
  fillerBytes = Math.max(0, (memory.free - 512) * memory.blocksize);
  var localFiller = new Uint8Array(fillerBytes);
  if (!localFiller) return false;
  if (measure) pressureFree = process.memory().free;
  try { XFSM.createMachine(config); }
  catch (error) { localError = error; }
  return isFallbackError(localError);
}

print("TEST=xfsm_host_diagnostic_pressure");
baselineUsage = process.memory().usage;
longEvent = Array(8001).join("E");
on = {};
on[longEvent] = { target: "Done" };
config = {
  initial: "Idle",
  states: {
    Idle: { on: on },
    Done: {}
  }
};
warmFallbackOk = attempt(true);
process.memory();
E.defrag();
process.memory();

prePressureFree = process.memory().free;
settledUsage = process.memory().usage;
// The first numeric assignment may itself allocate a JsVar on constrained targets.
settledUsage = process.memory().usage;
print("METRIC pre_pressure_free_blocks=" + prePressureFree);
fallbackOk = attempt(true);
print("METRIC pressure_free_blocks=" + pressureFree);
process.memory();
E.defrag();
process.memory();
postFailureUsage = process.memory().usage;
cleanupOk = postFailureUsage <= settledUsage;

config = undefined;
on = undefined;
longEvent = undefined;
process.memory();
E.defrag();
process.memory();
finalUsage = process.memory().usage;
passed = true;
passed = report("warmup_diagnostic_fallback", warmFallbackOk) && passed;
passed = report("diagnostic_fallback", fallbackOk) && passed;
passed = report("diagnostic_pressure_cleanup", cleanupOk) && passed;
print("METRIC baseline_usage_blocks=" + baselineUsage);
print("METRIC settled_usage_blocks=" + settledUsage);
print("METRIC pre_pressure_free_blocks=" + prePressureFree);
print("METRIC pressure_free_blocks=" + pressureFree);
print("METRIC filler_bytes=" + fillerBytes);
print("METRIC post_failure_usage_blocks=" + postFailureUsage);
print("METRIC final_usage_blocks=" + finalUsage);
result = passed;
print("DONE=" + (passed ? "PASS" : "FAIL"));
})();
