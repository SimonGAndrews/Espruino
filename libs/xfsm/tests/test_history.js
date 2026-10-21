/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

echo(false);
(function () {
var XFSM = require("XFSM");
var checks = [];

function marker(trace, name) {
  return function (context, event) { trace.push(name + ":" + event.type); };
}

function collect() {
  process.memory();
  if (E.defrag) E.defrag();
}

var resumeTrace = [];
var resumeMachine = XFSM.createMachine({
  id: "controller",
  initial: "working",
  states: {
    working: {
      id: "working",
      initial: { target: "fast", actions: "initialWorking" },
      entry: "enterWorking",
      exit: "exitWorking",
      states: {
        history: { type: "history" },
        fast: {
          entry: "enterFast", exit: "exitFast", on: { SLOW: "slow" }
        },
        slow: {
          entry: "enterSlow", exit: "exitSlow", on: { FAST: "fast" }
        }
      },
      on: { RESET: { target: "resetting", actions: "reset" } }
    },
    resetting: {
      entry: "enterResetting",
      exit: "exitResetting",
      on: {
        RESET_DONE: { target: "#working.history", actions: "resume" }
      }
    }
  }
}, {
  actions: {
    initialWorking: marker(resumeTrace, "initialWorking"),
    enterWorking: marker(resumeTrace, "enterWorking"),
    exitWorking: marker(resumeTrace, "exitWorking"),
    enterFast: marker(resumeTrace, "enterFast"),
    exitFast: marker(resumeTrace, "exitFast"),
    enterSlow: marker(resumeTrace, "enterSlow"),
    exitSlow: marker(resumeTrace, "exitSlow"),
    reset: marker(resumeTrace, "reset"),
    enterResetting: marker(resumeTrace, "enterResetting"),
    exitResetting: marker(resumeTrace, "exitResetting"),
    resume: marker(resumeTrace, "resume")
  }
});
var resumeActor = XFSM.createActor(resumeMachine).start();
resumeActor.send("SLOW");
resumeActor.send("RESET");
resumeActor.send("RESET_DONE");
var resumed = resumeActor.getSnapshot();
var resumeExpected = [
  "enterWorking:xstate.init",
  "initialWorking:xstate.init",
  "enterFast:xstate.init",
  "exitFast:SLOW",
  "enterSlow:SLOW",
  "exitSlow:RESET",
  "exitWorking:RESET",
  "reset:RESET",
  "enterResetting:RESET",
  "exitResetting:RESET_DONE",
  "resume:RESET_DONE",
  "enterWorking:RESET_DONE",
  "enterSlow:RESET_DONE"
];
checks.push(resumed.matches({ working: "slow" }) &&
  !("history" in resumed) &&
  resumeTrace.join("|") === resumeExpected.join("|"));
resumeActor.stop();
resumed = undefined;
resumeActor = undefined;
resumeMachine = undefined;
resumeTrace = undefined;
resumeExpected = undefined;
collect();

function firstUseMachine(customFallback, trace) {
  var history = { type: "history", history: "shallow" };
  if (customFallback) history.target = "slow";
  return XFSM.createMachine({
    id: customFallback ? "custom" : "normal",
    initial: "away",
    states: {
      away: { on: { GO: "#owner.history" } },
      owner: {
        id: "owner",
        initial: { target: "fast", actions: "initialOwner" },
        entry: "enterOwner",
        states: {
          history: history,
          fast: { entry: "enterFast" },
          slow: { entry: "enterSlow" }
        }
      }
    }
  }, {
    actions: {
      initialOwner: marker(trace, "initialOwner"),
      enterOwner: marker(trace, "enterOwner"),
      enterFast: marker(trace, "enterFast"),
      enterSlow: marker(trace, "enterSlow")
    }
  });
}

var normalTrace = [];
var normalActor = XFSM.createActor(firstUseMachine(false, normalTrace)).start();
normalActor.send("GO");
checks.push(normalActor.getSnapshot().matches({ owner: "fast" }) &&
  normalTrace.join("|") ===
    "enterOwner:GO|initialOwner:GO|enterFast:GO");
normalActor.stop();
normalActor = undefined;
normalTrace = undefined;
collect();

var customTrace = [];
var customActor = XFSM.createActor(firstUseMachine(true, customTrace)).start();
customActor.send("GO");
checks.push(customActor.getSnapshot().matches({ owner: "slow" }) &&
  customTrace.join("|") === "enterOwner:GO|enterSlow:GO");
customActor.stop();
customActor = undefined;
customTrace = undefined;
collect();

var compoundTrace = [];
var compoundMachine = XFSM.createMachine({
  id: "compoundFallback",
  initial: "away",
  states: {
    away: { on: { GO: "#owner2.history" } },
    owner: {
      id: "owner2",
      initial: "other",
      states: {
        history: { type: "history", target: "branch" },
        other: {},
        branch: {
          initial: { target: "middle", actions: "branchInitial" },
          entry: "enterBranch",
          states: {
            middle: {
              initial: { target: "leaf", actions: "middleInitial" },
              entry: "enterMiddle",
              states: { leaf: { entry: "enterLeaf" } }
            }
          }
        }
      }
    }
  }
}, {
  actions: {
    branchInitial: marker(compoundTrace, "branchInitial"),
    middleInitial: marker(compoundTrace, "middleInitial"),
    enterBranch: marker(compoundTrace, "enterBranch"),
    enterMiddle: marker(compoundTrace, "enterMiddle"),
    enterLeaf: marker(compoundTrace, "enterLeaf")
  }
});
var compoundActor = XFSM.createActor(compoundMachine).start();
compoundActor.send("GO");
checks.push(compoundActor.getSnapshot().matches({
  owner: { branch: { middle: "leaf" } }
}) && compoundTrace.join("|") ===
  "enterBranch:GO|enterMiddle:GO|middleInitial:GO|enterLeaf:GO");
compoundActor.stop();
compoundActor = undefined;
compoundMachine = undefined;
compoundTrace = undefined;
collect();

var nestedDefaultTrace = [];
var nestedDefaultMachine = XFSM.createMachine({
  id: "nestedDefaultFallback",
  initial: "away",
  states: {
    away: { on: { GO: "#nestedDefaultOwner.history" } },
    owner: {
      id: "nestedDefaultOwner",
      initial: { target: "branch", actions: "ownerInitial" },
      states: {
        history: { type: "history" },
        branch: {
          initial: { target: "middle", actions: "branchInitial" },
          entry: "enterBranch",
          states: {
            middle: {
              initial: { target: "leaf", actions: "middleInitial" },
              entry: "enterMiddle",
              states: { leaf: { entry: "enterLeaf" } }
            }
          }
        }
      }
    }
  }
}, {
  actions: {
    ownerInitial: marker(nestedDefaultTrace, "ownerInitial"),
    branchInitial: marker(nestedDefaultTrace, "branchInitial"),
    middleInitial: marker(nestedDefaultTrace, "middleInitial"),
    enterBranch: marker(nestedDefaultTrace, "enterBranch"),
    enterMiddle: marker(nestedDefaultTrace, "enterMiddle"),
    enterLeaf: marker(nestedDefaultTrace, "enterLeaf")
  }
});
var nestedDefaultActor = XFSM.createActor(nestedDefaultMachine).start();
nestedDefaultActor.send("GO");
checks.push(nestedDefaultActor.getSnapshot().matches({
  owner: { branch: { middle: "leaf" } }
}) && nestedDefaultTrace.join("|") ===
  "ownerInitial:GO|enterBranch:GO|enterMiddle:GO|" +
  "middleInitial:GO|enterLeaf:GO");
nestedDefaultActor.stop();
nestedDefaultActor = undefined;
nestedDefaultMachine = undefined;
nestedDefaultTrace = undefined;
collect();

function dataU16(data, offset) {
  return data.charCodeAt(offset) | (data.charCodeAt(offset + 1) << 8);
}

function actorHistory(actor) {
  var data = actor["\xFFxfaD"];
  return dataU16(data, dataU16(data, 12) & 1 ? 18 : 16);
}

var isolationMachine = XFSM.createMachine({
  id: "isolation",
  initial: "owner",
  states: {
    owner: {
      id: "isolatedOwner",
      initial: "a",
      states: {
        history: { type: "history" },
        a: { on: { NEXT: "b" } },
        b: { on: { PREVIOUS: "a" } }
      },
      on: { LEAVE: "away" }
    },
    away: { on: { RETURN: "#isolatedOwner.history" } }
  }
});
var actorA = XFSM.createActor(isolationMachine).start();
var actorB = XFSM.createActor(isolationMachine).start();
actorA.send("NEXT");
actorA.send("LEAVE");
actorB.send("LEAVE");
process.memory();
if (E.defrag) E.defrag();
actorA.send("RETURN");
actorB.send("RETURN");
checks.push(actorA.getSnapshot().matches({ owner: "b" }) &&
  actorB.getSnapshot().matches({ owner: "a" }));
actorA.send("PREVIOUS");
var historyBeforeStop = actorHistory(actorA);
actorA.stop();
checks.push(actorHistory(actorA) === historyBeforeStop &&
  actorA.getSnapshot().status === "stopped");
actorB.stop();
actorA = undefined;
actorB = undefined;
isolationMachine = undefined;
historyBeforeStop = undefined;
collect();

var faultMachine = XFSM.createMachine({
  id: "faultHistory",
  initial: "owner",
  states: {
    owner: {
      id: "faultOwner",
      initial: "fast",
      exit: "failExit",
      states: {
        history: { type: "history" },
        fast: { on: { SLOW: "slow" } },
        slow: { on: { FAST: "fast" } }
      },
      on: { EXIT: "away", FAIL: "away" }
    },
    away: { on: { RESUME: "#faultOwner.history" } }
  }
}, {
  actions: {
    failExit: function (context, event) {
      if (event.type === "FAIL") throw new Error("expected exit failure");
    }
  }
});
var faultActor = XFSM.createActor(faultMachine).start();
faultActor.send("SLOW");
faultActor.send("EXIT");
faultActor.send("RESUME");
faultActor.send("FAST");
var faultData = faultActor["\xFFxfaD"];
var beforeFlags = dataU16(faultData, 12);
var beforeHistory = dataU16(faultData, beforeFlags & 1 ? 18 : 16);
var faultError;
try { faultActor.send("FAIL"); } catch (error) { faultError = error; }
faultData = faultActor["\xFFxfaD"];
var afterFlags = dataU16(faultData, 12);
var committedHistory = dataU16(faultData, afterFlags & 1 ? 18 : 16);
var discardedHistory = dataU16(faultData, afterFlags & 1 ? 16 : 18);
checks.push(faultError instanceof Error &&
  faultActor.getSnapshot().status === "error" &&
  faultActor.getSnapshot().matches({ owner: "fast" }) &&
  afterFlags === beforeFlags && committedHistory === beforeHistory &&
  discardedHistory !== committedHistory);
faultActor = undefined;
faultMachine = undefined;
faultData = undefined;
faultError = undefined;
collect();

var nestedMachine = XFSM.createMachine({
  id: "nested",
  initial: "outer",
  states: {
    outer: {
      id: "outer",
      initial: "section",
      states: {
        history: { type: "history" },
        section: {
          id: "section",
          initial: "a",
          states: {
            history: { type: "history" },
            a: { on: { NEXT: "b" } },
            b: {}
          },
          on: { PAUSE: "pause" }
        },
        pause: { on: { CONTINUE: "#section.history" } }
      },
      on: { LEAVE: "away" }
    },
    away: { on: { RETURN: "#outer.history" } }
  }
});
var nestedActor = XFSM.createActor(nestedMachine).start();
nestedActor.send("NEXT");
nestedActor.send("PAUSE");
nestedActor.send("CONTINUE");
var innerRestored = nestedActor.getSnapshot().matches({
  outer: { section: "b" }
});
nestedActor.send("LEAVE");
nestedActor.send("RETURN");
checks.push(innerRestored && nestedActor.getSnapshot().matches({
  outer: { section: "a" }
}));
nestedActor.stop();
nestedActor = undefined;
nestedMachine = undefined;
collect();

var completionTrace = [];
var completionMachine = XFSM.createMachine({
  id: "completionHistory",
  initial: "workflow",
  states: {
    workflow: {
      id: "workflow",
      initial: "working",
      states: {
        history: { type: "history" },
        working: { on: { FINISH: "complete" } },
        complete: { type: "final", entry: "enterComplete" }
      },
      onDone: { target: "away", actions: "done" }
    },
    away: { on: { RESUME: "#workflow.history" } }
  }
}, {
  actions: {
    enterComplete: marker(completionTrace, "enterComplete"),
    done: marker(completionTrace, "done")
  }
});
var completionActor = XFSM.createActor(completionMachine).start();
completionActor.send("FINISH");
var completionStart = completionTrace.length;
completionActor.send("RESUME");
checks.push(completionActor.getSnapshot().value === "away" &&
  completionTrace.slice(completionStart).join("|") ===
    "enterComplete:RESUME|done:xstate.done.state.workflow");
if (checks[checks.length - 1] === false)
  print("COMPLETION=" + completionActor.getSnapshot().value + "|" +
        completionTrace.slice(completionStart).join("|"));
completionActor.stop();

result = checks.indexOf(false) < 0;
if (!result) print("CHECKS=" + checks.join(","));
print("TEST=xfsm_history");
print((result ? "PASS " : "FAIL ") + "history");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
