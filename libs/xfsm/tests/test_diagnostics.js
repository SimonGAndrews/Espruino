var XFSM = require("XFSM");

function fails(config, options, category, path) {
  try {
    XFSM.createMachine(config, options);
  } catch (error) {
    var message = "" + error;
    return message.indexOf(category) >= 0 && message.indexOf(path) >= 0;
  }
  return false;
}

var cyclic = { initial: "A", states: {} };
cyclic.states.A = cyclic;

result =
  fails({ states: { A: {} } }, undefined,
        "E_INITIAL_REQUIRED", "config.initial") &&
  fails({ initial: "Missing", states: { A: {} } }, undefined,
        "E_INITIAL_UNKNOWN", "config.initial") &&
  fails({ initial: "A", states: { A: { on: { GO: "Missing" } } } },
        undefined, "E_TARGET_UNKNOWN", "config.states.A.on.GO") &&
  fails({ initial: "A", states: { A: { entry: "missing" } } },
        { actions: {} }, "E_ACTION_UNRESOLVED", "config.states.A.entry") &&
  fails({ context: 1, initial: "A", states: { A: {} } }, undefined,
        "E_CONFIG_TYPE", "config.context") &&
  fails(cyclic, undefined, "E_CONFIG_TYPE", "config.states.A");
