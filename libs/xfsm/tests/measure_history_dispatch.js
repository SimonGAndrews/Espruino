/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* History-owner scaling for operations that do and do not use history. */
echo(false);
(function () {
var XFSM = require("XFSM");
var counts = [0, 1, 8, 32];
var sendIterations = 20000;
var pairIterations = 10000;

function addHistoryOwners(states, count) {
  var i;
  for (i = 0; i < count; i++) {
    var key = "owner" + i;
    states[key] = {
      id: key,
      initial: "a",
      states: {
        history: { type: "history" },
        a: { on: { NEXT: "b" } },
        b: { on: { NEXT: "a" } }
      },
      on: { EXIT: "live" }
    };
  }
}

function makeOrdinaryActor(count) {
  var states = {
    live: {
      initial: "a",
      states: {
        a: { on: { STEP: "b" } },
        b: { on: { STEP: "a" } }
      },
      on: { TARGETLESS: { actions: [] } }
    }
  };
  addHistoryOwners(states, count);
  return XFSM.createActor(XFSM.createMachine({
    id: "ordinary" + count,
    initial: "live",
    states: states
  })).start();
}

function makeHistoryActor(count) {
  var states = {
    live: { on: { RETURN: "#owner0.history" } }
  };
  addHistoryOwners(states, count);
  return XFSM.createActor(XFSM.createMachine({
    id: "history" + count,
    initial: "owner0",
    states: states
  })).start();
}

function timedSends(actor, event, iterations) {
  var i;
  for (i = 0; i < 100; i++) actor.send(event);
  var started = getTime();
  for (i = 0; i < iterations; i++) actor.send(event);
  return (getTime() - started) * 1000000 / iterations;
}

function timedPairs(actor, iterations) {
  var i;
  for (i = 0; i < 20; i++) {
    actor.send("EXIT");
    actor.send("RETURN");
  }
  var started = getTime();
  for (i = 0; i < iterations; i++) {
    actor.send("EXIT");
    actor.send("RETURN");
  }
  return (getTime() - started) * 1000000 / iterations;
}

var samples = [];
counts.forEach(function (count) {
  var actor = makeOrdinaryActor(count);
  var sample = {
    historyOwners: count,
    actorBytes: actor["\xFFxfaD"].length,
    unhandledUs: timedSends(actor, "UNKNOWN", sendIterations),
    targetlessUs: timedSends(actor, "TARGETLESS", sendIterations),
    ordinaryUs: timedSends(actor, "STEP", sendIterations)
  };
  actor.stop();
  actor = undefined;
  process.memory();
  if (count > 0) {
    actor = makeHistoryActor(count);
    sample.historyPairUs = timedPairs(actor, pairIterations);
    sample.historyCorrect = actor.getSnapshot().matches({ owner0: "a" });
    actor.stop();
  }
  samples.push(sample);
  actor = undefined;
  process.memory();
});

result = samples.every(function (sample) {
  return sample.actorBytes === 16 + sample.historyOwners * 4 &&
    sample.unhandledUs > 0 && sample.targetlessUs > 0 &&
    sample.ordinaryUs > 0 &&
    (sample.historyOwners === 0 ||
      (sample.historyPairUs > 0 && sample.historyCorrect));
});
print("HISTORY_DISPATCH=" + JSON.stringify({
  sendIterations: sendIterations,
  pairIterations: pairIterations,
  samples: samples
}));
print((result ? "PASS " : "FAIL ") + "history_dispatch");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
