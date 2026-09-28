/* XFC-CF-CONFIG-005: implementation-map grammar and ownership. */
(function () {
  var XFSM = require("XFSM");

  function errorMatches(error, category, path) {
    return error && error instanceof Error && !(error instanceof TypeError) &&
      ("" + error).indexOf("XFC " + category + " @ " + path) >= 0;
  }

  function rejects(name, config, options, category, path) {
    var caught;
    try {
      XFSM.createMachine(config, options);
    } catch (error) {
      caught = error;
    }
    if (caught) XFCTrace.error(name, category, path, caught);
    XFCTrace.assert(name, errorMatches(caught, category, path));
  }

  function retainedContains(machine, value) {
    var retained = machine["\xFFxfcR"];
    for (var index = 0; index < retained.length; index++) {
      if (retained[index] === value) return true;
    }
    return false;
  }

  XFCTrace.begin("XFC-CF-CONFIG-005",
    "implementation-map property grammar and ownership", "profile-internal");

  XFCTrace.assert("omitted and undefined maps accepted",
    !!XFSM.createMachine({}) &&
    !!XFSM.createMachine({}, { actions: undefined, guards: undefined }));
  rejects("null options rejected", {}, null,
    "E_CONFIG_TYPE", "options");
  rejects("array options rejected", {}, [],
    "E_CONFIG_TYPE", "options");
  rejects("function options rejected", {}, function () {},
    "E_CONFIG_TYPE", "options");
  rejects("array action map rejected", {}, { actions: [] },
    "E_CONFIG_TYPE", "options.actions");
  rejects("function guard map rejected", {}, { guards: function () {} },
    "E_CONFIG_TYPE", "options.guards");

  var calls = [];
  var exactAction = function (context, event) {
    calls.push("action:" + event.type);
  };
  var exactGuard = function (context, event) {
    calls.push("guard:" + event.type);
    return true;
  };
  var unusedAction = function () {
    calls.push("unused");
  };
  var actions = {
    "tools.work": exactAction,
    unused: unusedAction
  };
  var guards = { "rules.ready": exactGuard };
  var options = { actions: actions, guards: guards };
  var machine = XFSM.createMachine({
    initial: "Idle",
    states: {
      Idle: {
        on: {
          GO: {
            target: "Done",
            guard: "rules.ready",
            actions: "tools.work"
          }
        }
      },
      Done: {}
    }
  }, options);
  var actor = XFSM.createActor(machine).start();
  actor.send("GO");
  XFCTrace.assert("exact path-like names resolve",
    calls.join(",") === "guard:GO,action:GO" &&
    actor.getSnapshot().value === "Done");
  XFCTrace.assert("referenced functions retained",
    retainedContains(machine, exactAction) && retainedContains(machine, exactGuard));
  XFCTrace.assert("unused callable is not retained",
    !retainedContains(machine, unusedAction));
  XFCTrace.assert("source option graph is not retained",
    !retainedContains(machine, options) &&
    !retainedContains(machine, actions) &&
    !retainedContains(machine, guards));

  rejects("action names are case-sensitive", {
    initial: "Idle",
    states: { Idle: { entry: "Tools.work" } }
  }, { actions: actions }, "E_ACTION_UNRESOLVED", "config.states.Idle.entry");

  global.scopeOnlyAction = function () {};
  rejects("outer scope is not an implementation map", {
    initial: "Idle",
    states: { Idle: { entry: "scopeOnlyAction" } }
  }, { actions: {} }, "E_ACTION_UNRESOLVED", "config.states.Idle.entry");
  delete global.scopeOnlyAction;

  var inheritedActions = Object.create({ inherited: function () {} });
  rejects("inherited action is not an implementation", {
    initial: "Idle",
    states: { Idle: { entry: "inherited" } }
  }, { actions: inheritedActions }, "E_ACTION_UNRESOLVED",
  "config.states.Idle.entry");

  rejects("unused non-callable entry rejected", {
    initial: "Idle", states: { Idle: {} }
  }, { actions: { unused: 1 } }, "E_CONFIG_TYPE", "options.actions.unused");

  var accessorReads = 0;
  var accessorActions = {};
  Object.defineProperty(accessorActions, "work", {
    enumerable: true,
    get: function () {
      accessorReads++;
      return function () {};
    }
  });
  rejects("accessor entry rejected without invocation", {
    initial: "Idle", states: { Idle: { entry: "work" } }
  }, { actions: accessorActions }, "E_CONFIG_TYPE", "options.actions.work");
  XFCTrace.assert("implementation accessor was not called", accessorReads === 0);

  var nonEnumerableActions = {};
  Object.defineProperty(nonEnumerableActions, "hidden", {
    enumerable: false,
    value: function () {}
  });
  if (Object.keys(nonEnumerableActions).indexOf("hidden") < 0) {
    rejects("non-enumerable action is not an implementation", {
      initial: "Idle", states: { Idle: { entry: "hidden" } }
    }, { actions: nonEnumerableActions }, "E_ACTION_UNRESOLVED",
    "config.states.Idle.entry");
  } else {
    XFCTrace.skip("non-enumerable action is not an implementation",
      "host does not represent the enumerable descriptor flag");
  }

  if (typeof Symbol === "function") {
    var symbolActions = {};
    symbolActions[Symbol("work")] = function () {};
    var symbolMachine = XFSM.createMachine({
      initial: "Idle", states: { Idle: {} }
    }, { actions: symbolActions });
    XFCTrace.assert("symbol entry does not define an implementation",
      !!symbolMachine);
  } else {
    XFCTrace.skip("symbol entry does not define an implementation",
      "host has no Symbol value type");
  }

  XFCTrace.finish();
})();
