/* XFC-CF-TRANS-005: remaining event and guard contract. */
(function () {
  var XFSM = require("XFSM");

  function has(error, category, path) {
    return error &&
      ("" + error).indexOf("XFC " + category + " @ " + path) >= 0;
  }
  function rejects(name, operation, category, path) {
    var caught;
    try { operation(); } catch (error) { caught = error; }
    if (caught) XFCTrace.error(name, category, path, caught);
    XFCTrace.assert(name, has(caught, category, path));
  }

  XFCTrace.begin("XFC-CF-TRANS-005",
    "event identity, validation, guard coercion and fallback", "profile");

  var calls = [];
  var suppliedEvent;
  var guardEvent;
  var actionEvent;
  var stringGuardEvent;
  var stringActionEvent;
  var machine = XFSM.createMachine({
    initial: "Parent",
    states: {
      Parent: {
        initial: "Child",
        states: {
          Child: {
            on: {
              MUTATE: {
                guard: function (context, event) {
                  guardEvent = event;
                  event.type = "REDIRECT";
                  calls.push("mutateGuard");
                  return {};
                },
                actions: function (context, event) {
                  actionEvent = event;
                  calls.push("mutateAction:" + event.type + ":" + event.payload);
                }
              },
              STRING: {
                guard: function (context, event) {
                  stringGuardEvent = event;
                  return 1;
                },
                actions: function (context, event) {
                  stringActionEvent = event;
                  calls.push("string:" + event.type);
                }
              },
              FALL: [
                { guard: function () { calls.push("false0"); return 0; }, actions: function () { calls.push("wrong0"); } },
                { guard: function () { calls.push("falseEmpty"); return ""; }, actions: function () { calls.push("wrongEmpty"); } }
              ],
              "*": [
                { guard: function () { calls.push("wildFalse"); return null; } },
                { guard: { type: "truthy" }, actions: { type: "wildAction" } }
              ],
              BLOCK: undefined
            }
          }
        },
        on: {
          FALL: { actions: function () { calls.push("parentWrong"); } },
          BLOCK: { actions: function () { calls.push("blockedWrong"); } },
          PARENT: { guard: "falseGuard", actions: function () { calls.push("parentFalseWrong"); } }
        }
      }
    }
  }, {
    guards: {
      truthy: function (context, event) {
        calls.push("wildTruthy");
        return event.type === "PARENT" ? 0 : "yes";
      },
      falseGuard: function () { calls.push("parentFalse"); return false; }
    },
    actions: {
      wildAction: function (context, event) { calls.push("wildAction:" + event.type); }
    }
  });
  var actor = XFSM.createActor(machine).start();
  suppliedEvent = { type: "MUTATE", payload: 7 };
  actor.send(suppliedEvent);
  XFCTrace.assert("object identity and type capture",
    guardEvent === suppliedEvent && actionEvent === suppliedEvent &&
    suppliedEvent.type === "REDIRECT" &&
    calls.join(",") === "mutateGuard,mutateAction:REDIRECT:7");
  actor.send("STRING");
  XFCTrace.assert("string event materialized once",
    stringGuardEvent === stringActionEvent && stringGuardEvent.type === "STRING");
  actor.send("FALL");
  XFCTrace.assert("false exact candidates fall to wildcard",
    calls.slice(-5).join(",") ===
      "false0,falseEmpty,wildFalse,wildTruthy,wildAction:FALL");
  var beforeBlock = calls.length;
  actor.send("BLOCK");
  XFCTrace.assert("present undefined handler forbids fallback",
    calls.length === beforeBlock);
  actor.send("PARENT");
  XFCTrace.assert("all-false parent candidate is unhandled",
    calls.slice(-3).join(",") === "wildFalse,wildTruthy,parentFalse");

  var stable = actor.getSnapshot();
  var invalid = [undefined, null, {}, { type: 1 }, "", { type: "" },
    [], function () {}, 1, true, { type: "xstate.private" },
    { type: "@xstate.private" }];
  var invalidOk = true;
  var invalidResults = [];
  var invalidTypes = [];
  for (var index = 0; index < invalid.length; index++) {
    var caught;
    try { actor.send(invalid[index]); } catch (error) { caught = error; }
    var accepted = has(caught, "E_EVENT_INVALID", "actor.send.event");
    invalidResults.push(accepted);
    invalidTypes.push(caught instanceof TypeError);
    if (!accepted || !(caught instanceof TypeError)) invalidOk = false;
  }
  XFCTrace.context("invalid event observations", {
    rejected: invalidResults,
    typeErrors: invalidTypes,
    stableSnapshot: actor.getSnapshot() === stable
  });
  XFCTrace.assert("malformed and reserved events reject before dispatch",
    invalidOk && actor.getSnapshot() === stable);

  rejects("empty candidate array rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { on: { GO: [] } } } });
  }, "E_CONFIG_TYPE", "config.states.A.on.GO");
  rejects("null target rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { on: { GO: { target: null } } } } });
  }, "E_CONFIG_TYPE", "config.states.A.on.GO.target");
  rejects("partial wildcard rejected", function () {
    XFSM.createMachine({ initial: "A", states: { A: { on: { "sensor.*": {} } } } });
  }, "E_UNSUPPORTED_FEATURE", "config.states.A.on[\"sensor.*\"]");
  rejects("guard params rejected", function () {
    XFSM.createMachine({
      initial: "A",
      states: { A: { on: { GO: { guard: { type: "ready", params: {} } } } } }
    },
      { guards: { ready: function () { return true; } } });
  }, "E_UNKNOWN_PROPERTY", "config.states.A.on.GO.guard.params");

  XFCTrace.finish();
})();
