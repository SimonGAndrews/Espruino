/* XFC-CF-COMPAT-006: pinned XState 4.38.3 migration differential. */
(function () {
  var XFSM = require("XFSM");
  var observed = [];

  function mark(name) {
    return function (context, event) {
      observed.push(name);
      XFCTrace.action(name, undefined, event.type);
    };
  }

  XFCTrace.begin("XFC-CF-COMPAT-006",
    "retained XState 4.38.3 migration aliases", "differential");
  XFCTrace.context("reference", {
    engine: "xstate",
    version: "4.38.3",
    adaptations: ["actor lifecycle API", "imports and module wrapper"]
  });

  var actor = XFSM.createActor(XFSM.createMachine({
    predictableActionArguments: true,
    preserveActionOrder: true,
    initial: "Active",
    states: {
      Active: {
        entry: mark("entry"),
        exit: mark("exit"),
        on: {
          CHECK: { cond: "allowed", actions: mark("checked") },
          REENTER: { target: "Active", internal: false, actions: mark("external") },
          PRESERVE: { target: "Active", internal: true, actions: mark("internal") }
        }
      }
    }
  }, { guards: { allowed: function () { return true; } } })).start();
  observed = [];
  actor.send("CHECK");
  actor.send("REENTER");
  actor.send("PRESERVE");
  XFCTrace.snapshot("migration result", actor.getSnapshot());
  XFCTrace.assert("cond and internal aliases",
    observed.join("|") === "checked|exit|external|entry|internal",
    observed.join("|"), "checked|exit|external|entry|internal");
  XFCTrace.finish();
})();
