/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Matched shallow/deep resource harness. Requires XFC_MEASURE=1. */
echo(false);
(function () {
var XFSM = require("XFSM");

function config(mode) {
  return {
    id: "historyModeResource",
    initial: "owner",
    states: {
      owner: {
        id: "historyModeOwner",
        initial: "branch",
        states: {
          history: { type: "history", history: mode },
          branch: {
            initial: "phaseA",
            states: {
              phaseA: { on: { NEXT: "phaseB" } },
              phaseB: {}
            }
          }
        },
        on: { EXIT: "away" }
      },
      away: { on: { RETURN: "#historyModeOwner.history" } }
    }
  };
}

function measureMachine(mode) {
  process.memory();
  var before = XFSM._memoryUsage();
  var configuration = config(mode);
  XFSM._measure(true);
  var machine = XFSM.createMachine(configuration);
  var compile = XFSM._measure(false);
  configuration = undefined;
  process.memory();
  var after = XFSM._memoryUsage();
  var actorBefore = XFSM._memoryUsage();
  var actor = XFSM.createActor(machine);
  process.memory();
  var actorAfter = XFSM._memoryUsage();
  var actorBytes = actor["\xFFxfaD"].length;

  actor.start();
  actor.send("NEXT");
  actor.send("EXIT");
  XFSM._measure(true, true);
  actor.send("RETURN");
  var restore = XFSM._measure(false);
  var restored = actor.getSnapshot();
  var correct = mode === "deep"
    ? restored.matches({ owner: { branch: "phaseB" } })
    : restored.matches({ owner: { branch: "phaseA" } });

  actor.stop();
  return {
    machine: machine,
    actor: actor,
    metrics: {
      arenaBytes: machine["\xFFxfcA"].length,
      retainedValues: machine["\xFFxfcR"].length,
      machinePersistentBlocks: after - before,
      constructionPeakBlocks: compile.constructionPeakBlocks,
      actorPersistentBlocks: actorAfter - actorBefore,
      actorBytes: actorBytes,
      restorePeakBlocks: restore.operationPeakBlocks,
      restoreEndBlocks: restore.operationEndBlocks,
      restoreStackBytes: restore.lastStackBytes,
      correct: correct
    }
  };
}

var warmActor = XFSM.createActor(XFSM.createMachine(config("deep"))).start();
warmActor.send("NEXT");
warmActor.send("EXIT");
warmActor.send("RETURN");
warmActor.stop();
warmActor = undefined;
process.memory();

var shallow = measureMachine("shallow");
var deep = measureMachine("deep");
var report = {
  shallow: shallow.metrics,
  deep: deep.metrics,
  delta: {
    arenaBytes: deep.metrics.arenaBytes - shallow.metrics.arenaBytes,
    machinePersistentBlocks:
      deep.metrics.machinePersistentBlocks -
      shallow.metrics.machinePersistentBlocks,
    constructionPeakBlocks:
      deep.metrics.constructionPeakBlocks -
      shallow.metrics.constructionPeakBlocks,
    actorPersistentBlocks:
      deep.metrics.actorPersistentBlocks -
      shallow.metrics.actorPersistentBlocks,
    actorBytes: deep.metrics.actorBytes - shallow.metrics.actorBytes,
    restorePeakBlocks:
      deep.metrics.restorePeakBlocks - shallow.metrics.restorePeakBlocks,
    restoreStackBytes:
      deep.metrics.restoreStackBytes - shallow.metrics.restoreStackBytes
  }
};

result = shallow.metrics.correct && deep.metrics.correct;
result = result && shallow.metrics.actorBytes === 20;
result = result && deep.metrics.actorBytes === 20;
result = result && report.delta.arenaBytes === 0;
result = result && report.delta.actorBytes === 0;
print("DEEP_HISTORY_RESOURCE=" + JSON.stringify(report));
print((result ? "PASS " : "FAIL ") + "deep_history_resource");
print("DONE=" + (result ? "PASS" : "FAIL"));

shallow.actor = undefined;
shallow.machine = undefined;
deep.actor = undefined;
deep.machine = undefined;
shallow = undefined;
deep = undefined;
process.memory();
})();
