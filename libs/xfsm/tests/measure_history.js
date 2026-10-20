/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Experimental shallow-history resource harness. Requires XFC_MEASURE=1. */
echo(false);
(function () {
var XFSM = require("XFSM");

function config(withHistory) {
  var states = {
    fast: { on: { SLOW: "slow" } },
    slow: { on: { FAST: "fast" } }
  };
  if (withHistory) states.history = { type: "history" };
  return {
    id: withHistory ? "historyResource" : "baseResource",
    initial: "owner",
    states: {
      owner: {
        id: withHistory ? "historyOwner" : "baseOwner",
        initial: "fast",
        states: states,
        on: { EXIT: "away" }
      },
      away: {
        on: {
          RETURN: withHistory ? "#historyOwner.history" : "#baseOwner.fast"
        }
      }
    }
  };
}

function measureMachine(withHistory) {
  process.memory();
  var before = XFSM._memoryUsage();
  var configuration = config(withHistory);
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
  actor.send("SLOW");
  actor.send("EXIT");
  XFSM._measure(true, true);
  actor.send("RETURN");
  var restore = XFSM._measure(false);
  var restored = actor.getSnapshot();
  var correct = withHistory
    ? restored.matches({ owner: "slow" })
    : restored.matches({ owner: "fast" });

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

var warmActor = XFSM.createActor(XFSM.createMachine({
  initial: "idle", states: { idle: {} }
}));
warmActor = undefined;
process.memory();

var base = measureMachine(false);
var history = measureMachine(true);
var report = {
  base: base.metrics,
  history: history.metrics,
  delta: {
    arenaBytes: history.metrics.arenaBytes - base.metrics.arenaBytes,
    machinePersistentBlocks:
      history.metrics.machinePersistentBlocks -
      base.metrics.machinePersistentBlocks,
    constructionPeakBlocks:
      history.metrics.constructionPeakBlocks -
      base.metrics.constructionPeakBlocks,
    actorPersistentBlocks:
      history.metrics.actorPersistentBlocks -
      base.metrics.actorPersistentBlocks,
    actorBytes: history.metrics.actorBytes - base.metrics.actorBytes,
    restorePeakBlocks:
      history.metrics.restorePeakBlocks - base.metrics.restorePeakBlocks,
    restoreStackBytes:
      history.metrics.restoreStackBytes - base.metrics.restoreStackBytes
  }
};

result = base.metrics.correct && history.metrics.correct &&
  base.metrics.actorBytes === 16 && history.metrics.actorBytes === 20 &&
  report.delta.actorBytes === 4;
print("HISTORY_RESOURCE=" + JSON.stringify(report));
print((result ? "PASS " : "FAIL ") + "history_resource");
print("DONE=" + (result ? "PASS" : "FAIL"));

base.actor = undefined;
base.machine = undefined;
history.actor = undefined;
history.machine = undefined;
base = undefined;
history = undefined;
process.memory();
})();
