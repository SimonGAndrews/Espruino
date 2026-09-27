echo(false);
(function () {
var XFSM = require("XFSM");
var passed = typeof XFSM._failNext === "undefined";

function fails(config, options, category, path) {
  try { XFSM.createMachine(config, options); }
  catch (error) {
    var message = "" + error;
    return error instanceof Error && !(error instanceof TypeError) &&
      message.indexOf("XFC " + category + " @ " + path) >= 0;
  }
  return false;
}

passed = passed && fails({
  intial: "Idle",
  initial: "Idle",
  states: { Idle: {} }
}, undefined, "E_UNKNOWN_PROPERTY", "config.intial");
passed = passed && fails({
  initial: "Idle",
  states: { Idle: { invoke: {} } }
}, undefined, "E_UNSUPPORTED_FEATURE", "config.states.Idle.invoke");
passed = passed && fails({
  initial: "Idle",
  states: { Idle: { type: "parallel" } }
}, undefined, "E_UNSUPPORTED_FEATURE", "config.states.Idle.type");
passed = passed && fails({
  initial: "Idle",
  states: { Idle: { on: { GO: { target: ["Idle"] } } } }
}, undefined, "E_UNSUPPORTED_FEATURE", "config.states.Idle.on.GO.target");
passed = passed && fails({
  initial: "Idle",
  states: { Idle: {} }
}, { actors: { work: function () {} } },
"E_UNSUPPORTED_FEATURE", "options.actors");

var reads = 0;
var accessor = { states: { Idle: {} } };
Object.defineProperty(accessor, "initial", {
  enumerable: true,
  get: function () { reads++; return "Idle"; }
});
passed = passed && fails(accessor, undefined,
  "E_CONFIG_TYPE", "config.initial") && reads === 0;
accessor = undefined;

var calls = 0;
var machine = XFSM.createMachine({
  context: function () { calls++; return {}; },
  initial: "Idle",
  states: {
    Idle: {
      entry: function () { calls++; },
      on: { GO: { guard: function () { calls++; return true; } } }
    }
  },
  meta: {},
  description: "accepted"
}, { actions: {}, guards: {}, services: {}, actors: {}, delays: {} });
passed = passed && !!machine && calls === 0;
machine = undefined;

var detail = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
var detailMessage = "";
try {
  XFSM.createMachine({
    initial: "Idle",
    states: { Idle: { entry: detail } }
  }, { actions: {} });
} catch (error) { detailMessage = "" + error; }
var suffix = detailMessage.substring(detailMessage.lastIndexOf(": ") + 2);
passed = passed && suffix === "\"" + detail.substring(0, 48) + "\"";

result = passed;
print("TEST=xfsm_strict_validation_embedded");
print((result ? "PASS " : "FAIL ") + "strict_validation_embedded");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
