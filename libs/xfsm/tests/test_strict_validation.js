echo(false);
(function () {
var XFSM = require("XFSM");
var checks = [];

function fails(config, options, category, path) {
  try {
    XFSM.createMachine(config, options);
  } catch (error) {
    var message = "" + error;
    var ok = error instanceof Error && !(error instanceof TypeError) &&
      message.indexOf("XFC " + category + " @ " + path) >= 0;
    if (!ok) print("MISMATCH=" + category + "|" + path + "|" + message);
    return ok;
  }
  print("NO_ERROR=" + category + "|" + path);
  return false;
}

function base(state) {
  return { initial: "Idle", states: { Idle: state || {} } };
}

checks.push(fails({
  intial: "Idle",
  bogus: true,
  initial: "Idle",
  states: { Idle: {} }
}, undefined, "E_UNKNOWN_PROPERTY", "config.intial"));
checks.push(fails(base({ on: { GO: { gaurd: "ready" } } }), {
  guards: { ready: function () { return true; } }
}, "E_UNKNOWN_PROPERTY", "config.states.Idle.on.GO.gaurd"));
checks.push(fails({
  initial: "Parent state",
  states: { "Parent state": { entyr: "bad" } }
}, undefined, "E_UNKNOWN_PROPERTY", "config.states[\"Parent state\"].entyr"));
checks.push(fails(base(), { unknown: {} },
  "E_UNKNOWN_PROPERTY", "options.unknown"));

var unsupported = ["invoke", "after", "always", "activities", "tags",
                   "history", "delimiter"];
for (var i = 0; i < unsupported.length; i++) {
  var state = {};
  state[unsupported[i]] = {};
  checks.push(fails(base(state), undefined, "E_UNSUPPORTED_FEATURE",
    "config.states.Idle." + unsupported[i]));
}
checks.push(fails(base({ type: "parallel" }), undefined,
  "E_UNSUPPORTED_FEATURE", "config.states.Idle.type"));
checks.push(fails(base({ type: "history" }), undefined,
  "E_UNSUPPORTED_FEATURE", "config.states.Idle.type"));
checks.push(fails(base({ output: 1 }), undefined,
  "E_UNSUPPORTED_FEATURE", "config.states.Idle.output"));
checks.push(fails(base(), { services: { work: function () {} } },
  "E_UNSUPPORTED_FEATURE", "options.services"));
checks.push(fails(base(), { actors: { work: function () {} } },
  "E_UNSUPPORTED_FEATURE", "options.actors"));
checks.push(fails(base(), { delays: { short: 10 } },
  "E_UNSUPPORTED_FEATURE", "options.delays"));
checks.push(fails(base({ meta: { owner: "test" } }), undefined,
  "E_UNSUPPORTED_FEATURE", "config.states.Idle.meta"));
checks.push(fails(base({ on: { GO: { target: ["Idle"] } } }), undefined,
  "E_UNSUPPORTED_FEATURE", "config.states.Idle.on.GO.target"));

checks.push(fails(base({ on: { GO: [] } }), undefined,
  "E_CONFIG_TYPE", "config.states.Idle.on.GO"));
checks.push(fails(base({ on: { GO: { params: {} } } }), undefined,
  "E_UNKNOWN_PROPERTY", "config.states.Idle.on.GO.params"));
checks.push(fails(base({ on: { GO: { actions: { type: "work", exec: 1 } } } }), {
  actions: { work: function () {} }
}, "E_UNKNOWN_PROPERTY", "config.states.Idle.on.GO.actions.exec"));
checks.push(fails(base({ on: { GO: { guard: { type: "ready", params: {} } } } }), {
  guards: { ready: function () { return true; } }
}, "E_UNKNOWN_PROPERTY", "config.states.Idle.on.GO.guard.params"));
checks.push(fails({
  initial: { target: "Idle", guard: "ready" },
  states: { Idle: {} }
}, { guards: { ready: function () { return true; } } },
  "E_UNKNOWN_PROPERTY", "config.initial.guard"));
checks.push(fails(base({ on: { GO: { guard: "a", cond: "b" } } }), {
  guards: {
    a: function () { return true; },
    b: function () { return true; }
  }
}, "E_CONFIG_TYPE", "config.states.Idle.on.GO.guard"));
checks.push(fails(base({ on: { GO: { reenter: true, internal: false } } }),
  undefined, "E_CONFIG_TYPE", "config.states.Idle.on.GO.reenter"));

checks.push(fails({
  type: "atomic",
  initial: "Idle",
  states: { Idle: {} }
}, undefined, "E_CONFIG_TYPE", "config.states"));
checks.push(fails({ type: "final" }, undefined,
  "E_CONFIG_TYPE", "config.type"));
checks.push(fails(base({ type: "compound", states: {} }), undefined,
  "E_CONFIG_TYPE", "config.states.Idle.states"));
checks.push(fails(base({ description: 1 }), undefined,
  "E_CONFIG_TYPE", "config.states.Idle.description"));
checks.push(fails({
  predictableActionArguments: false,
  initial: "Idle",
  states: { Idle: {} }
}, undefined, "E_CONFIG_TYPE", "config.predictableActionArguments"));

var rootReads = 0;
var rootAccessor = { states: { Idle: {} } };
Object.defineProperty(rootAccessor, "initial", {
  enumerable: true,
  get: function () { rootReads++; return "Idle"; }
});
checks.push(fails(rootAccessor, undefined, "E_CONFIG_TYPE", "config.initial") &&
            rootReads === 0);

var childReads = 0;
var childStates = {};
Object.defineProperty(childStates, "Idle", {
  enumerable: true,
  get: function () { childReads++; return {}; }
});
checks.push(fails({ initial: "Idle", states: childStates }, undefined,
  "E_CONFIG_TYPE", "config.states.Idle") && childReads === 0);

var eventReads = 0;
var eventMap = {};
Object.defineProperty(eventMap, "GO", {
  enumerable: true,
  get: function () { eventReads++; return {}; }
});
checks.push(fails(base({ on: eventMap }), undefined,
  "E_CONFIG_TYPE", "config.states.Idle.on.GO") && eventReads === 0);

var transitionReads = 0;
var transitionAccessor = {};
Object.defineProperty(transitionAccessor, "target", {
  enumerable: true,
  get: function () { transitionReads++; return "Idle"; }
});
checks.push(fails(base({ on: { GO: transitionAccessor } }), undefined,
  "E_CONFIG_TYPE", "config.states.Idle.on.GO.target") &&
  transitionReads === 0);

var optionReads = 0;
var actionMap = {};
Object.defineProperty(actionMap, "work", {
  enumerable: true,
  get: function () { optionReads++; return function () {}; }
});
checks.push(fails(base(), { actions: actionMap },
  "E_CONFIG_TYPE", "options.actions.work") && optionReads === 0);

var calls = 0;
var callbackMachine = XFSM.createMachine({
  context: function () { calls++; return { count: 0 }; },
  initial: "Idle",
  states: {
    Idle: {
      entry: function () { calls++; },
      on: {
        GO: {
          guard: function () { calls++; return true; },
          actions: XFSM.assign({ count: function () { calls++; return 1; } })
        }
      }
    }
  },
  predictableActionArguments: true,
  preserveActionOrder: true,
  meta: {},
  description: "accepted inert fields"
}, { actions: {}, guards: {}, services: {}, actors: {}, delays: {} });
checks.push(!!callbackMachine && calls === 0);

var shared = { description: "shared fragment" };
checks.push(!!XFSM.createMachine({
  initial: "A",
  states: { A: shared, B: shared }
}));

var unicodeKey = "\u20AC";
var unicodeEvent = unicodeKey + " event";
var unicodeStates = { Start: {} };
unicodeStates.Start.on = {};
unicodeStates.Start.on[unicodeEvent] = unicodeKey;
unicodeStates[unicodeKey] = {};
var unicodeActor = XFSM.createActor(XFSM.createMachine({
  initial: "Start",
  states: unicodeStates
})).start();
unicodeActor.send(unicodeEvent);
checks.push(unicodeActor.getSnapshot().matches(unicodeKey));

var detail = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
var detailError;
try {
  XFSM.createMachine(base({ entry: detail }), { actions: {} });
} catch (error) { detailError = "" + error; }
var suffix = detailError.substring(detailError.lastIndexOf(": ") + 2);
checks.push(suffix === "\"" + detail.substring(0, 48) + "\"");

var unicodeDetail = "a";
for (var unicodeIndex = 0; unicodeIndex < 16; unicodeIndex++)
  unicodeDetail += unicodeKey;
var unicodeDetailError;
try {
  XFSM.createMachine(base({ entry: unicodeDetail }), { actions: {} });
} catch (error) { unicodeDetailError = "" + error; }
var unicodeSuffix = unicodeDetailError.substring(
  unicodeDetailError.lastIndexOf(": ") + 2);
var expectedUnicodeSuffix = "\"a";
for (unicodeIndex = 0; unicodeIndex < 15; unicodeIndex++)
  expectedUnicodeSuffix += "\\xE2\\x82\\xAC";
expectedUnicodeSuffix += "\"";
var unicodeDetailOk = unicodeSuffix === expectedUnicodeSuffix;
if (!unicodeDetailOk)
  print("UNICODE_DETAIL=" + unicodeSuffix + "|" + expectedUnicodeSuffix);
checks.push(unicodeDetailOk);

result = checks.indexOf(false) < 0;
if (!result) print("CHECKS=" + checks.join(","));
print("TEST=xfsm_strict_validation");
print((result ? "PASS " : "FAIL ") + "strict_validation");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
