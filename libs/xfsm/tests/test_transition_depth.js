echo(false);
var XFSM = require("XFSM");
var trace = [];
var initialMemory = process.memory();
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

var actor = XFSM.createActor(machine).start();
var startupActions = trace.length;
var startupUsage = process.memory().usage;
var startupOk = startupActions === 32;
for (var i = 0; startupOk && i < 32; i++) {
  startupOk = trace[i] === "enter";
}

trace = [];
actor.send("RESET");
var transitionActions = trace.length;
var transitionUsage = process.memory().usage;
var transitionOk = transitionActions === 65 && trace[32] === "reset";
var firstTransitionMismatch = -1;
for (var j = 0; transitionOk && j < 32; j++) {
  if (trace[j] !== "exit" || trace[j + 33] !== "enter") {
    transitionOk = false;
    firstTransitionMismatch = j;
  }
}

var activeOk = actor.getSnapshot().matches("L1");
result = startupOk && transitionOk && activeOk;
print("TEST=xfsm_transition_depth");
print((result ? "PASS " : "FAIL ") + "transition_depth");
print("METRIC total_blocks=" + initialMemory.total);
print("METRIC initial_usage_blocks=" + initialMemory.usage);
print("METRIC startup_usage_blocks=" + startupUsage);
print("METRIC startup_actions=" + startupActions);
print("METRIC startup_ok=" + startupOk);
print("METRIC transition_usage_blocks=" + transitionUsage);
print("METRIC transition_actions=" + transitionActions);
print("METRIC transition_ok=" + transitionOk);
print("METRIC first_transition_mismatch=" + firstTransitionMismatch);
print("METRIC active_ok=" + activeOk);
print("DONE=" + (result ? "PASS" : "FAIL"));
actor.stop();
actor = undefined;
machine = undefined;
leaf = undefined;
parent = undefined;
trace = undefined;
XFSM = undefined;
