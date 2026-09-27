echo(false);
(function () {
var XFSM = require("XFSM");
var trace = [];
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
var startupOk = trace.length === 32;
for (var i = 0; startupOk && i < 32; i++)
  startupOk = trace[i] === "enter";

trace = [];
actor.send("RESET");
var transitionOk = trace.length === 65 && trace[32] === "reset";
for (var j = 0; transitionOk && j < 32; j++)
  transitionOk = trace[j] === "exit" && trace[j + 33] === "enter";

result = startupOk && transitionOk && actor.getSnapshot().matches("L1");
print("TEST=xfsm_transition_depth");
print((result ? "PASS " : "FAIL ") + "transition_depth");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
