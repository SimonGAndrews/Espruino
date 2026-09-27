/* M5 completion-chain timing harness. Requires XFC_MEASURE=1. */
echo(false);
print("TEST=xfsm_m5_completion");
(function () {
var XFSM = require("XFSM");

function makeLoopMachine(limit, unlimited) {
  return XFSM.createMachine({
    context: function () { return { count: 0 }; },
    initial: "Idle",
    states: {
      Idle: { on: { GO: "Loop" } },
      Loop: {
        initial: "Complete",
        states: { Complete: { type: "final" } },
        onDone: {
          target: "Loop",
          guard: "again",
          actions: XFSM.assign({
            count: function (context) { return context.count + 1; }
          })
        }
      }
    }
  }, {
    guards: {
      again: function (context) {
        return unlimited || context.count < limit;
      }
    }
  });
}

XFSM._measure(true);
var boundaryMachine = makeLoopMachine(255, false);
var boundaryActor = XFSM.createActor(boundaryMachine).start();
var boundaryStarted = getTime();
boundaryActor.send("GO");
var boundaryElapsed = (getTime() - boundaryStarted) * 1000000;
var boundaryMetrics = XFSM._measure(false);
var boundarySnapshot = boundaryActor.getSnapshot();
var boundary = {
  microsteps: 256,
  elapsedMicroseconds: boundaryElapsed,
  maximumStackBytes: boundaryMetrics.maximumStackBytes,
  arenaBytes: boundaryMachine["\xFFxfcA"].length,
  contextCount: boundarySnapshot.context.count,
  stable: boundarySnapshot.status === "active" &&
          boundarySnapshot.matches({ Loop: "Complete" }) &&
          boundarySnapshot.context.count === 255
};
boundaryActor.stop();
boundaryActor = undefined;
boundaryMachine = undefined;
boundarySnapshot = undefined;
process.memory();

XFSM._measure(true);
var overflowMachine = makeLoopMachine(0, true);
var overflowActor = XFSM.createActor(overflowMachine).start();
var overflowStable = overflowActor.getSnapshot();
var overflowMessage = "";
var overflowStarted = getTime();
try { overflowActor.send("GO"); }
catch (error) { overflowMessage = "" + error; }
var overflowElapsed = (getTime() - overflowStarted) * 1000000;
var overflowMetrics = XFSM._measure(false);
var overflowSnapshot = overflowActor.getSnapshot();
var overflow = {
  rejectedMicrostep: 257,
  elapsedMicroseconds: overflowElapsed,
  maximumStackBytes: overflowMetrics.maximumStackBytes,
  message: overflowMessage,
  status: overflowSnapshot.status,
  stableStateRetained: overflowSnapshot.value === "Idle" &&
                       overflowSnapshot.context === overflowStable.context &&
                       overflowSnapshot.context.count === 0
};

var report = { completionBoundary: boundary, completionLimit: overflow };
print("M5_COMPLETION_RESULT=" + JSON.stringify(report));
print("METRIC boundary_us=" + boundary.elapsedMicroseconds);
print("METRIC limit_us=" + overflow.elapsedMicroseconds);
print("METRIC maximum_stack_bytes=" + Math.max(
  boundary.maximumStackBytes, overflow.maximumStackBytes));
result = boundary.stable &&
  overflow.message.indexOf("E_MICROSTEP_LIMIT @ actor.send: max=256") >= 0 &&
  overflow.status === "error" && overflow.stableStateRetained;
print((result ? "PASS " : "FAIL ") + "completion_chain");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
