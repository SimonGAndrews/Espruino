/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Matched shallow/deep dispatch timing. Runs on normal production firmware. */
echo(false);
(function () {
var XFSM = require("XFSM");
var iterations = 500;

function makeActor(mode) {
  return XFSM.createActor(XFSM.createMachine({
    id: "historyTiming",
    initial: "owner",
    states: {
      owner: {
        id: "historyTimingOwner",
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
      away: { on: { RETURN: "#historyTimingOwner.history" } }
    }
  })).start();
}

function measure(mode) {
  var actor = makeActor(mode);
  var i;
  actor.send("NEXT");
  for (i = 0; i < 10; i++) {
    actor.send("EXIT");
    actor.send("RETURN");
  }
  var start = getTime();
  for (i = 0; i < iterations; i++) {
    actor.send("EXIT");
    actor.send("RETURN");
  }
  var elapsed = getTime() - start;
  var expected = mode === "deep" ? "phaseB" : "phaseA";
  var correct = actor.getSnapshot().matches({ owner: { branch: expected } });
  actor.stop();
  return {
    seconds: elapsed,
    microsecondsPerPair: elapsed * 1000000 / iterations,
    correct: correct
  };
}

var shallow = measure("shallow");
var deep = measure("deep");
var report = {
  iterations: iterations,
  shallow: shallow,
  deep: deep,
  deltaMicrosecondsPerPair:
    deep.microsecondsPerPair - shallow.microsecondsPerPair
};
result = shallow.correct && deep.correct;
result = result && shallow.seconds > 0 && deep.seconds > 0;
print("DEEP_HISTORY_TIMING=" + JSON.stringify(report));
print((result ? "PASS " : "FAIL ") + "deep_history_timing");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
