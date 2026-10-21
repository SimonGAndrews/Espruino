/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Target-sized deep-history semantics, split into collectable cases. */
echo(false);
var XFSM = require("XFSM");
var deepHistoryChecks = [];

function deepHistoryCollect() {
  process.memory();
  if (E.defrag) E.defrag();
}

function deepHistoryU16(data, offset) {
  return data.charCodeAt(offset) | (data.charCodeAt(offset + 1) << 8);
}

/* Restore the exact leaf without running initial-transition actions. */
(function () {
  var trace = [];
  function mark(name) { return function () { trace.push(name); }; }
  var machine = XFSM.createMachine({
    id: "deepRestore", initial: "working", states: {
      working: {
        id: "deepRestoreWorking", entry: "workingIn", exit: "workingOut",
        initial: { target: "mode", actions: "workingInitial" }, states: {
          history: { type: "history", history: "deep" },
          mode: {
            entry: "modeIn", exit: "modeOut",
            initial: { target: "phaseA", actions: "modeInitial" }, states: {
              phaseA: {
                entry: "phaseAIn", exit: "phaseAOut", on: { NEXT: "phaseB" }
              },
              phaseB: { entry: "phaseBIn", exit: "phaseBOut" }
            }
          }
        }, on: { EXIT: "outside" }
      },
      outside: { entry: "outsideIn", exit: "outsideOut", on: {
        RESUME: { target: "#deepRestoreWorking.history", actions: "resume" }
      } }
    }
  }, { actions: {
    workingIn: mark("workingIn"), workingOut: mark("workingOut"),
    workingInitial: mark("workingInitial"), modeIn: mark("modeIn"),
    modeOut: mark("modeOut"), modeInitial: mark("modeInitial"),
    phaseAIn: mark("phaseAIn"), phaseAOut: mark("phaseAOut"),
    phaseBIn: mark("phaseBIn"), phaseBOut: mark("phaseBOut"),
    outsideIn: mark("outsideIn"), outsideOut: mark("outsideOut"),
    resume: mark("resume")
  }});
  var actor = XFSM.createActor(machine).start();
  actor.send("NEXT");
  actor.send("EXIT");
  trace = [];
  actor.send("RESUME");
  deepHistoryChecks.push(actor.getSnapshot().matches({
    working: { mode: "phaseB" }
  }) && trace.join("|") ===
    "outsideOut|resume|workingIn|modeIn|phaseBIn");
  actor.stop();
})();
deepHistoryCollect();

/* Compare normal initial entry with a descendant first-use fallback. */
(function () {
  function run(custom) {
    var trace = [];
    function mark(name) { return function () { trace.push(name); }; }
    var history = { type: "history", history: "deep" };
    if (custom) history.target = "branch.phaseB";
    var machine = XFSM.createMachine({
      id: custom ? "deepCustom" : "deepNormal", initial: "outside", states: {
        outside: { on: { GO: "#deepFallbackOwner.history" } },
        owner: {
          id: "deepFallbackOwner", entry: "ownerIn",
          initial: { target: "branch", actions: "ownerInitial" }, states: {
            history: history,
            branch: {
              entry: "branchIn",
              initial: { target: "phaseA", actions: "branchInitial" },
              states: {
                phaseA: { entry: "phaseAIn" },
                phaseB: { entry: "phaseBIn" }
              }
            }
          }
        }
      }
    }, { actions: {
      ownerIn: mark("ownerIn"), ownerInitial: mark("ownerInitial"),
      branchIn: mark("branchIn"), branchInitial: mark("branchInitial"),
      phaseAIn: mark("phaseAIn"), phaseBIn: mark("phaseBIn")
    }});
    var actor = XFSM.createActor(machine).start();
    actor.send("GO");
    var ok = custom
      ? actor.getSnapshot().matches({ owner: { branch: "phaseB" } }) &&
        trace.join("|") === "ownerIn|branchIn|phaseBIn"
      : actor.getSnapshot().matches({ owner: { branch: "phaseA" } }) &&
        trace.join("|") === "ownerIn|ownerInitial|branchIn|phaseAIn";
    if (!ok) print("FALLBACK=" + custom + "|" + trace.join("|") + "|" +
      JSON.stringify(actor.getSnapshot().value));
    actor.stop();
    return ok;
  }
  deepHistoryChecks.push(run(false) && run(true));
})();
deepHistoryCollect();

/* Nested owners retain and restore their exact leaves independently. */
(function () {
  var machine = XFSM.createMachine({
    id: "deepNested", initial: "outer", states: {
      outer: {
        id: "deepOuter", initial: "inner", states: {
          outerHistory: { type: "history", history: "deep" },
          inner: {
            id: "deepInner", initial: "phaseA", states: {
              innerHistory: { type: "history", history: "deep" },
              phaseA: { on: { NEXT: "phaseB" } },
              phaseB: { on: { PARK: "#deepOuter.parked" } }
            }
          },
          parked: { on: { RETURN_INNER: "#deepInner.innerHistory" } }
        }, on: { LEAVE: "away" }
      },
      away: { on: { RETURN_OUTER: "#deepOuter.outerHistory" } }
    }
  });
  var actor = XFSM.createActor(machine).start();
  actor.send("NEXT");
  actor.send("LEAVE");
  actor.send("RETURN_OUTER");
  var outer = actor.getSnapshot().matches({ outer: { inner: "phaseB" } });
  actor.send("PARK");
  actor.send("RETURN_INNER");
  deepHistoryChecks.push(outer &&
    actor.getSnapshot().matches({ outer: { inner: "phaseB" } }));
  actor.stop();
})();
deepHistoryCollect();

/* Restoring a final leaf runs completion before publishing a snapshot. */
(function () {
  var trace = [];
  function mark(name) { return function () { trace.push(name); }; }
  var machine = XFSM.createMachine({
    id: "deepFinal", initial: "task", states: {
      task: {
        id: "deepFinalTask", entry: "taskIn", exit: "taskOut",
        initial: "phase", states: {
          history: { type: "history", history: "deep" },
          phase: {
            entry: "phaseIn", exit: "phaseOut", initial: "active", states: {
              active: { exit: "activeOut", on: { FINISH: "completed" } },
              completed: {
                type: "final", entry: "completedIn", exit: "completedOut"
              }
            }, onDone: { target: "#deepFinal.holding", actions: "phaseDone" }
          }
        }
      },
      holding: { entry: "holdingIn", exit: "holdingOut", on: {
        RESUME: "#deepFinalTask.history"
      } }
    }
  }, { actions: {
    taskIn: mark("taskIn"), taskOut: mark("taskOut"),
    phaseIn: mark("phaseIn"), phaseOut: mark("phaseOut"),
    activeOut: mark("activeOut"), completedIn: mark("completedIn"),
    completedOut: mark("completedOut"), phaseDone: mark("phaseDone"),
    holdingIn: mark("holdingIn"), holdingOut: mark("holdingOut")
  }});
  var actor = XFSM.createActor(machine).start();
  actor.send("FINISH");
  trace = [];
  actor.send("RESUME");
  deepHistoryChecks.push(actor.getSnapshot().matches("holding") &&
    trace.join("|") ===
      "holdingOut|taskIn|phaseIn|completedIn|completedOut|phaseOut|taskOut|phaseDone|holdingIn");
  actor.stop();
})();
deepHistoryCollect();

/* A failed exit changes only the uncommitted history bank. */
(function () {
  var machine = XFSM.createMachine({
    id: "deepFault", initial: "working", states: {
      working: {
        id: "deepFaultWorking", initial: "mode", exit: "fail", states: {
          history: { type: "history", history: "deep" },
          mode: { initial: "phaseA", states: {
            phaseA: { on: { NEXT: "phaseB" } },
            phaseB: { on: { BACK: "phaseA" } }
          } }
        }, on: { EXIT: "outside", FAIL: "outside" }
      },
      outside: { on: { RESUME: "#deepFaultWorking.history" } }
    }
  }, { actions: { fail: function (context, event) {
    if (event.type === "FAIL") throw "deep-history-fault";
  }}});
  var actor = XFSM.createActor(machine).start();
  actor.send("NEXT");
  actor.send("EXIT");
  actor.send("RESUME");
  actor.send("BACK");
  /* Actor flags are at byte 12; the one-slot banks start at 16 and 18. */
  var data = actor["\xFFxfaD"];
  var flags = deepHistoryU16(data, 12);
  var committed = deepHistoryU16(data, flags & 1 ? 18 : 16);
  var error;
  try { actor.send("FAIL"); } catch (caught) { error = caught; }
  deepHistoryChecks.push(error === "deep-history-fault" &&
    actor.getSnapshot().status === "error" &&
    actor.getSnapshot().matches({ working: { mode: "phaseA" } }) &&
    deepHistoryU16(data, 12) === flags &&
    deepHistoryU16(data, flags & 1 ? 18 : 16) === committed &&
    deepHistoryU16(data, flags & 1 ? 16 : 18) !== committed);
})();

result = deepHistoryChecks.indexOf(false) < 0;
result = result && deepHistoryChecks.length === 5;
if (!result) print("CHECKS=" + deepHistoryChecks.join(","));
print("TEST=xfsm_deep_history");
print((result ? "PASS " : "FAIL ") + "deep_history");
print("DONE=" + (result ? "PASS" : "FAIL"));
