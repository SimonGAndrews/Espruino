echo(false);
(function () {
var XFSM = require("XFSM");
var checks = [];

function fails(config, category, path) {
  try {
    XFSM.createMachine(config);
  } catch (error) {
    var message = "" + error;
    return error instanceof Error && !(error instanceof TypeError) &&
      message.indexOf("XFC " + category + " @ " + path) >= 0;
  }
  return false;
}

function base(on) {
  return {
    initial: "Idle",
    states: { Idle: on ? { on: on } : {} }
  };
}

function nested(depth) {
  var node = {};
  for (var index = depth - 1; index >= 1; index--) {
    var child = {};
    child["S" + (index + 1)] = node;
    node = { initial: "S" + (index + 1), states: child };
  }
  var states = {};
  states.S1 = node;
  return { initial: "S1", states: states };
}

checks.push(!!XFSM.createMachine(nested(32)));
checks.push(fails(nested(33), "E_LIMIT_EXCEEDED", "config.states.S1"));

var longEvent = "x";
for (var power = 0; power < 16; power++) longEvent += longEvent;
var maxEvent = longEvent.substring(1);
var maxOn = {};
maxOn[maxEvent] = {};
checks.push(!!XFSM.createMachine(base(maxOn)));
var overOn = {};
overOn[longEvent] = {};
checks.push(fails(base(overOn), "E_LIMIT_EXCEEDED",
  "config.states.Idle.on"));

var actor = XFSM.createActor(XFSM.createMachine(base())).start();
var runtimeLimitError;
actor.send(maxEvent);
try { actor.send(longEvent); }
catch (error) { runtimeLimitError = error; }
checks.push(runtimeLimitError instanceof Error &&
  !(runtimeLimitError instanceof TypeError) &&
  ("" + runtimeLimitError).indexOf("E_LIMIT_EXCEEDED") >= 0 &&
  actor.getSnapshot().status === "active");

var unicodeLong = "\u20AC";
for (power = 0; power < 15; power++) unicodeLong += unicodeLong;
var unicodeOn = {};
unicodeOn[unicodeLong] = {};
checks.push(fails(base(unicodeOn), "E_LIMIT_EXCEEDED",
  "config.states.Idle.on"));

result = checks.indexOf(false) < 0;
if (!result) print("CHECKS=" + checks.join(","));
print("TEST=xfsm_limits");
print((result ? "PASS " : "FAIL ") + "limits");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
