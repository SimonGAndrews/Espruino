echo(false);
(function () {
var XFSM = require("XFSM");

function fails(config, options, category, path) {
  try {
    XFSM.createMachine(config, options);
  } catch (error) {
    var message = "" + error;
    var matches = message.indexOf(category) >= 0 &&
                  message.indexOf(path) >= 0;
    if (!matches)
      print("MISMATCH=" + category + "|" + path + "|" + message);
    return matches;
  }
  print("NO_ERROR=" + category + "|" + path);
  return false;
}

var ambiguous = {
  initial: "Source",
  states: {
    Source: {
      initial: "Idle",
      states: {
        Idle: {},
        "A.B": {},
        A: { initial: "B", states: { B: {} } }
      },
      on: { GO: ".A.B" }
    }
  }
};

result =
  fails(ambiguous, undefined, "E_TARGET_AMBIGUOUS",
        "config.states.Source.on.GO") &&
  fails({ initial: "A", states: { A: { id: "same" }, B: { id: "same" } } },
        undefined, "E_ID_DUPLICATE", "config.states.B.id") &&
  fails({ initial: "A", states: { A: { on: { GO: "B\\" } }, B: {} } },
        undefined, "E_CONFIG_TYPE", "config.states.A.on.GO") &&
  fails({ initial: "A", states: { A: { on: { GO: "#missing.Child" } } } },
        undefined, "E_TARGET_UNKNOWN", "config.states.A.on.GO") &&
  fails({ initial: "A", states: { A: { on: { "sensor.*": {} } } } },
        undefined, "E_UNSUPPORTED_FEATURE", "config.states.A.on") &&
  fails({ initial: "A", states: { A: { on: { GO: {
          guard: function () { return true; },
          cond: function () { return true; }
        } } } } }, undefined, "E_CONFIG_TYPE", "guard") &&
  fails({ initial: "A", states: { A: { on: { GO: {
          target: "A", reenter: true, internal: false
        } } } } }, undefined, "E_CONFIG_TYPE", "reenter") &&
  fails({ initial: "A", states: { A: { on: { GO: {
          target: "A", internal: "false"
        } } } } }, undefined, "E_CONFIG_TYPE", "internal") &&
  fails({ predictableActionArguments: false,
          initial: "A", states: { A: {} } }, undefined,
        "E_CONFIG_TYPE", "config.predictableActionArguments") &&
  fails({ preserveActionOrder: false,
          initial: "A", states: { A: {} } }, undefined,
        "E_CONFIG_TYPE", "config.preserveActionOrder") &&
  fails({ initial: "A", states: { A: {} } },
        { services: { work: function () {} } },
        "E_UNSUPPORTED_FEATURE", "options.services") &&
  fails({ initial: "A", states: { A: {} } },
        { actors: { work: function () {} } },
        "E_UNSUPPORTED_FEATURE", "options.actors") &&
  fails({ initial: "A", states: { A: {} } },
        { delays: { wait: 1 } },
        "E_UNSUPPORTED_FEATURE", "options.delays");
print("TEST=xfsm_profile1_diagnostics");
print((result ? "PASS " : "FAIL ") + "profile1_diagnostics");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
