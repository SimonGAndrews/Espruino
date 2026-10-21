/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Target-sized shallow-history coverage with independently collected cases. */
echo(false);
var XFSM = require("XFSM");
var historyChecks = [];

function historyCollect() {
  process.memory();
  if (E.defrag) E.defrag();
}

function historyU16(data, offset) {
  return data.charCodeAt(offset) | (data.charCodeAt(offset + 1) << 8);
}

/* Restore the latest child and verify action order. */
(function () {
  var trace = [];
  function mark(name) { return function () { trace.push(name); }; }
  var machine = XFSM.createMachine({
    id: "restore", initial: "owner", states: {
      owner: {
        id: "restoreOwner", initial: "fast", entry: "ownerIn",
        exit: "ownerOut", states: {
          history: { type: "history" },
          fast: { entry: "fastIn", exit: "fastOut", on: { SLOW: "slow" } },
          slow: { entry: "slowIn", exit: "slowOut" }
        }, on: { LEAVE: { target: "away", actions: "leave" } }
      },
      away: { on: { RETURN: {
        target: "#restoreOwner.history", actions: "resume"
      } } }
    }
  }, { actions: {
    ownerIn: mark("ownerIn"), ownerOut: mark("ownerOut"),
    fastIn: mark("fastIn"), fastOut: mark("fastOut"),
    slowIn: mark("slowIn"), slowOut: mark("slowOut"),
    leave: mark("leave"), resume: mark("resume")
  }});
  var actor = XFSM.createActor(machine).start();
  actor.send("SLOW");
  actor.send("LEAVE");
  actor.send("RETURN");
  historyChecks.push(actor.getSnapshot().matches({ owner: "slow" }) &&
    trace.join("|") ===
      "ownerIn|fastIn|fastOut|slowIn|slowOut|ownerOut|leave|resume|ownerIn|slowIn");
  actor.stop();
})();
historyCollect();

/* Exercise normal and explicit first-use fallbacks separately. */
(function () {
  function run(custom) {
    var history = { type: "history", history: "shallow" };
    if (custom) history.target = "slow";
    var machine = XFSM.createMachine({
      id: custom ? "custom" : "normal", initial: "away", states: {
        away: { on: { GO: "#fallbackOwner.history" } },
        owner: {
          id: "fallbackOwner", initial: "fast",
          states: { history: history, fast: {}, slow: {} }
        }
      }
    });
    var actor = XFSM.createActor(machine).start();
    actor.send("GO");
    var ok = actor.getSnapshot().matches({
      owner: custom ? "slow" : "fast"
    });
    actor.stop();
    return ok;
  }
  historyChecks.push(run(false));
  historyCollect();
  historyChecks.push(run(true));
})();
historyCollect();

/* Verify compound fallback descent and initial-action compatibility. */
(function () {
  var trace = [];
  function mark(name) { return function () { trace.push(name); }; }
  var machine = XFSM.createMachine({
    id: "shallow", initial: "away", states: {
      away: { on: { GO: "#shallowOwner.history" } },
      owner: {
        id: "shallowOwner", initial: "other", states: {
          history: { type: "history", target: "branch" }, other: {},
          branch: {
            initial: { target: "middle", actions: "branchInitial" },
            states: { middle: {
              initial: { target: "leaf", actions: "middleInitial" },
              states: { leaf: {} }
            } }
          }
        }
      }
    }
  }, { actions: {
    branchInitial: mark("branchInitial"),
    middleInitial: mark("middleInitial")
  }});
  var actor = XFSM.createActor(machine).start();
  actor.send("GO");
  historyChecks.push(actor.getSnapshot().matches({
    owner: { branch: { middle: "leaf" } }
  }) && trace.join("|") === "middleInitial");
  actor.stop();
})();
historyCollect();

/* Verify actor isolation and preservation of history across stop(). */
(function () {
  var machine = XFSM.createMachine({
    id: "isolation", initial: "owner", states: {
      owner: {
        id: "isolationOwner", initial: "a", states: {
          history: { type: "history" },
          a: { on: { NEXT: "b" } }, b: { on: { PREVIOUS: "a" } }
        }, on: { LEAVE: "away" }
      }, away: { on: { RETURN: "#isolationOwner.history" } }
    }
  });
  var first = XFSM.createActor(machine).start();
  var second = XFSM.createActor(machine).start();
  var data = first["\xFFxfaD"];
  var beforeOrdinaryFlags = historyU16(data, 12);
  first.send("UNKNOWN");
  first.send("NEXT");
  var afterOrdinaryFlags = historyU16(data, 12);
  first.send("LEAVE");
  var afterHistoryFlags = historyU16(data, 12);
  second.send("LEAVE");
  historyCollect();
  first.send("RETURN");
  second.send("RETURN");
  /* Actor flags are at byte 12; the one-slot banks start at 16 and 18. */
  first.send("PREVIOUS");
  var bank = historyU16(data, 12) & 1;
  var beforeStop = historyU16(data, bank ? 18 : 16);
  first.stop();
  historyChecks.push(first.getSnapshot().matches({ owner: "a" }) &&
    second.getSnapshot().matches({ owner: "a" }) &&
    beforeOrdinaryFlags === afterOrdinaryFlags &&
    afterHistoryFlags !== afterOrdinaryFlags &&
    historyU16(data, bank ? 18 : 16) === beforeStop);
  second.stop();
})();
historyCollect();

/* Verify that a failed exit cannot replace committed history. */
(function () {
  var machine = XFSM.createMachine({
    id: "fault", initial: "owner", states: {
      owner: {
        id: "faultOwner", initial: "fast", exit: "fail", states: {
          history: { type: "history" },
          fast: { on: { SLOW: "slow" } }, slow: { on: { FAST: "fast" } }
        }, on: { EXIT: "away", FAIL: "away" }
      }, away: { on: { RETURN: "#faultOwner.history" } }
    }
  }, { actions: { fail: function (context, event) {
    if (event.type === "FAIL") throw "history-fault";
  }}});
  var actor = XFSM.createActor(machine).start();
  actor.send("SLOW");
  actor.send("EXIT");
  actor.send("RETURN");
  actor.send("FAST");
  var data = actor["\xFFxfaD"];
  var flags = historyU16(data, 12);
  var committed = historyU16(data, flags & 1 ? 18 : 16);
  var error;
  try { actor.send("FAIL"); } catch (caught) { error = caught; }
  historyChecks.push(error === "history-fault" &&
    actor.getSnapshot().status === "error" &&
    actor.getSnapshot().matches({ owner: "fast" }) &&
    historyU16(data, 12) === flags &&
    historyU16(data, flags & 1 ? 18 : 16) === committed &&
    historyU16(data, flags & 1 ? 16 : 18) !== committed);
})();
historyCollect();

/* Verify independent shallow restoration for nested owners. */
(function () {
  var machine = XFSM.createMachine({
    id: "nested", initial: "outer", states: {
      outer: {
        id: "outer", initial: "section", states: {
          history: { type: "history" },
          section: {
            id: "section", initial: "a", states: {
              history: { type: "history" },
              a: { on: { NEXT: "b" } }, b: {}
            }, on: { PAUSE: "pause" }
          }, pause: { on: { CONTINUE: "#section.history" } }
        }, on: { LEAVE: "away" }
      }, away: { on: { RETURN: "#outer.history" } }
    }
  });
  var actor = XFSM.createActor(machine).start();
  actor.send("NEXT");
  actor.send("PAUSE");
  actor.send("CONTINUE");
  var inner = actor.getSnapshot().matches({ outer: { section: "b" } });
  actor.send("LEAVE");
  actor.send("RETURN");
  historyChecks.push(inner &&
    actor.getSnapshot().matches({ outer: { section: "a" } }));
  actor.stop();
})();
historyCollect();

/* Restoring a final child immediately completes its owner again. */
(function () {
  var machine = XFSM.createMachine({
    id: "finalHistory", initial: "owner", states: {
      owner: {
        id: "finalOwner", initial: "active", states: {
          history: { type: "history" },
          active: { on: { FINISH: "complete" } },
          complete: { type: "final" }
        }, onDone: "away"
      }, away: { on: { RETURN: "#finalOwner.history" } }
    }
  });
  var actor = XFSM.createActor(machine).start();
  actor.send("FINISH");
  actor.send("RETURN");
  historyChecks.push(actor.getSnapshot().matches("away"));
  actor.stop();
})();

result = historyChecks.indexOf(false) < 0 && historyChecks.length === 8;
if (!result) print("CHECKS=" + historyChecks.join(","));
print("TEST=xfsm_history_embedded");
print((result ? "PASS " : "FAIL ") + "history_embedded");
print("DONE=" + (result ? "PASS" : "FAIL"));
