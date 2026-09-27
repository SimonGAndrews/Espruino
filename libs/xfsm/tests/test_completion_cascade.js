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

function marker(trace, name) {
  return function (context, event) {
    trace.push(name + ":" + event.type);
  };
}

var cascadeTrace = [];
var cascadeMachine = XFSM.createMachine({
  id: "cascade",
  initial: "Outer",
  states: {
    Outer: {
      initial: "Middle",
      exit: "exitOuter",
      states: {
        Middle: {
          initial: "InnerFinal",
          exit: "exitMiddle",
          states: {
            InnerFinal: {
              type: "final",
              entry: "enterInnerFinal",
              exit: "exitInnerFinal"
            }
          },
          onDone: { target: "OuterFinal", actions: "middleDone" }
        },
        OuterFinal: {
          type: "final",
          entry: "enterOuterFinal",
          exit: "exitOuterFinal"
        }
      },
      onDone: { target: "Success", actions: "outerDone" }
    },
    Success: { entry: "enterCascadeSuccess" }
  }
}, {
  actions: {
    enterInnerFinal: marker(cascadeTrace, "enterInnerFinal"),
    exitInnerFinal: marker(cascadeTrace, "exitInnerFinal"),
    exitMiddle: marker(cascadeTrace, "exitMiddle"),
    middleDone: marker(cascadeTrace, "middleDone"),
    enterOuterFinal: marker(cascadeTrace, "enterOuterFinal"),
    exitOuterFinal: marker(cascadeTrace, "exitOuterFinal"),
    exitOuter: marker(cascadeTrace, "exitOuter"),
    outerDone: marker(cascadeTrace, "outerDone"),
    enterCascadeSuccess: marker(cascadeTrace, "enterCascadeSuccess")
  }
});
var cascadeActor = XFSM.createActor(cascadeMachine).start();
var cascadeSnapshot = cascadeActor.getSnapshot();
var middleEvent = "xstate.done.state.cascade.Outer.Middle";
var outerEvent = "xstate.done.state.cascade.Outer";
var cascadeExpected = [
  "enterInnerFinal:xstate.init",
  "exitInnerFinal:" + middleEvent,
  "exitMiddle:" + middleEvent,
  "middleDone:" + middleEvent,
  "enterOuterFinal:" + middleEvent,
  "exitOuterFinal:" + outerEvent,
  "exitOuter:" + outerEvent,
  "outerDone:" + outerEvent,
  "enterCascadeSuccess:" + outerEvent
];
var cascadeOk = cascadeSnapshot.status === "active" &&
  cascadeSnapshot.value === "Success" &&
  cascadeTrace.join("|") === cascadeExpected.join("|");
cascadeActor.stop();
cascadeMachine = undefined;
cascadeActor = undefined;
cascadeSnapshot = undefined;
cascadeTrace = undefined;
cascadeExpected = undefined;
process.memory();

var initialDoneTrace = [];
var initialDoneNotices = [];
var initialDoneMachine = XFSM.createMachine({
  entry: "enterInitialRoot",
  exit: "exitInitialRoot",
  initial: "Complete",
  states: {
    Complete: {
      type: "final",
      entry: "enterInitialComplete",
      exit: "exitInitialComplete"
    }
  }
}, {
  actions: {
    enterInitialRoot: marker(initialDoneTrace, "enterInitialRoot"),
    exitInitialRoot: marker(initialDoneTrace, "exitInitialRoot"),
    enterInitialComplete: marker(initialDoneTrace, "enterInitialComplete"),
    exitInitialComplete: marker(initialDoneTrace, "exitInitialComplete")
  }
});
var initialDoneActor = XFSM.createActor(initialDoneMachine);
initialDoneActor.subscribe(function (snapshot) {
  initialDoneNotices.push(snapshot.status);
});
initialDoneActor.start();
var initialDoneSnapshot = initialDoneActor.getSnapshot();
var initialDoneExpected = [
  "enterInitialRoot:xstate.init",
  "enterInitialComplete:xstate.init",
  "exitInitialComplete:xstate.init",
  "exitInitialRoot:xstate.init"
];
var initialDoneOk = initialDoneSnapshot.status === "done" &&
  initialDoneSnapshot.value === "Complete" &&
  initialDoneNotices.join("|") === "done" &&
  initialDoneTrace.join("|") === initialDoneExpected.join("|");

result = cascadeOk && initialDoneOk;
print("TEST=xfsm_completion_cascade");
print((result ? "PASS " : "FAIL ") + "completion_cascade");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
