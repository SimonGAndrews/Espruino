/* XFC-CF-CONFIG-004: complete state-node and initial-transition grammar. */
(function () {
  var XFSM = require("XFSM");

  function errorMatches(error, category, path) {
    return error && error instanceof Error && !(error instanceof TypeError) &&
      ("" + error).indexOf("XFC " + category + " @ " + path) >= 0;
  }

  function accepts(name, config, options) {
    var machine;
    try {
      machine = XFSM.createMachine(config, options);
    } catch (error) {
      XFCTrace.error(name, "unexpected", name, error);
    }
    XFCTrace.assert(name, !!machine);
    return machine;
  }

  function rejects(name, config, category, path) {
    var caught;
    try {
      XFSM.createMachine(config);
    } catch (error) {
      caught = error;
    }
    if (caught) XFCTrace.error(name, category, path, caught);
    XFCTrace.assert(name, errorMatches(caught, category, path));
  }

  XFCTrace.begin("XFC-CF-CONFIG-004",
    "state-node, initial-transition and composition grammar", "profile");

  rejects("missing config rejected", undefined,
    "E_CONFIG_TYPE", "config");
  rejects("null config rejected", null,
    "E_CONFIG_TYPE", "config");
  rejects("array config rejected", [],
    "E_CONFIG_TYPE", "config");
  rejects("function config rejected", function () {},
    "E_CONFIG_TYPE", "config");
  accepts("inferred atomic root", {});
  accepts("inferred atomic empty states", { states: {} });
  accepts("explicit atomic", { type: "atomic", states: {} });
  accepts("inferred compound", {
    initial: "A",
    states: { A: {}, B: {} }
  });
  accepts("explicit compound", {
    type: "compound",
    initial: "A",
    states: { A: { type: "atomic" } }
  });
  accepts("non-root final", {
    initial: "Done",
    states: { Done: { type: "final" } }
  });

  var initialCalls = 0;
  var initialMachine = accepts("initial descriptor accepted", {
    initial: {
      target: "Heating.Mode",
      actions: "prepare",
      description: "select the punctuated child",
      meta: {}
    },
    states: {
      "Heating.Mode": { entry: "enter" },
      Unused: {}
    },
    description: "composed machine",
    meta: {}
  }, {
    actions: {
      prepare: function (context, event) {
        initialCalls++;
        XFCTrace.action("prepare", initialCalls, event.type);
      },
      enter: function (context, event) {
        initialCalls++;
        XFCTrace.action("enter", initialCalls, event.type);
      }
    }
  });
  XFCTrace.assert("construction does not call actions", initialCalls === 0);
  var initialActor = XFSM.createActor(initialMachine).start();
  XFCTrace.snapshot("initial descriptor start", initialActor.getSnapshot(),
    initialCalls);
  XFCTrace.assert("initial action order and exact punctuated key",
    initialCalls === 2 && initialActor.getSnapshot().value === "Heating.Mode");

  var fragmentCalls = 0;
  function makeFragment() {
    fragmentCalls++;
    return { description: "shared construction fragment" };
  }
  var shared = makeFragment();
  var composedConfig = {
    initial: "A",
    states: { A: shared, B: shared }
  };
  var composedMachine = accepts("shared fragment composition", composedConfig);
  shared.description = "changed after construction";
  composedConfig.initial = "B";
  var composedActor = XFSM.createActor(composedMachine).start();
  XFCTrace.assert("fragment factory is application work", fragmentCalls === 1);
  XFCTrace.assert("compiled structure is source independent",
    composedActor.getSnapshot().value === "A");

  var nestedMachine = XFSM.createMachine({});
  rejects("compiled machine is not a nested state", {
    initial: "Nested",
    states: { Nested: nestedMachine }
  }, "E_CONFIG_TYPE", "config.states.Nested");
  rejects("root final rejected", { type: "final" },
    "E_CONFIG_TYPE", "config.type");
  rejects("atomic children rejected", {
    type: "atomic", states: { A: {} }
  }, "E_CONFIG_TYPE", "config.states");
  rejects("atomic initial rejected", { type: "atomic", initial: "A" },
    "E_CONFIG_TYPE", "config.initial");
  rejects("compound children required", { type: "compound", initial: "A" },
    "E_CONFIG_TYPE", "config.states");
  rejects("compound initial required", { states: { A: {} } },
    "E_INITIAL_REQUIRED", "config.initial");
  rejects("empty child key rejected", {
    initial: "", states: { "": {} }
  }, "E_CONFIG_TYPE", "config.states[\"\"]");
  rejects("null child rejected", {
    initial: "A", states: { A: null }
  }, "E_CONFIG_TYPE", "config.states.A");
  rejects("non-root context rejected", {
    initial: "A", states: { A: { context: {} } }
  }, "E_UNKNOWN_PROPERTY", "config.states.A.context");
  rejects("final event map rejected", {
    initial: "A", states: { A: { type: "final", on: {} } }
  }, "E_CONFIG_TYPE", "config.states.A.on");
  rejects("atomic onDone rejected", {
    initial: "A", states: { A: { onDone: "A" } }
  }, "E_CONFIG_TYPE", "config.states.A.onDone");
  rejects("initial descriptor target required", {
    initial: { actions: [] }, states: { A: {} }
  }, "E_CONFIG_TYPE", "config.initial.target");
  rejects("initial descriptor guard rejected", {
    initial: { target: "A", guard: "ready" }, states: { A: {} }
  }, "E_UNKNOWN_PROPERTY", "config.initial.guard");
  rejects("initial target must be direct child", {
    initial: { target: ".A" }, states: { A: {} }
  }, "E_INITIAL_UNKNOWN", "config.initial.target");

  XFCTrace.finish();
})();
