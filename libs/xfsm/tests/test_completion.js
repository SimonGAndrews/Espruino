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

var workflowTrace = [];
var workflowNotifications = [];
var workflowMachine = XFSM.createMachine({
  id: "Final state and parent done transition",
  initial: "Workflow",
  states: {
    Workflow: {
      initial: "Working",
      entry: "enterWorkflow",
      exit: "exitWorkflow",
      states: {
        Working: {
          entry: "enterWorking",
          exit: "exitWorking",
          on: {
            finish: { target: "Completed", actions: "recordFinish" }
          }
        },
        Completed: {
          type: "final",
          entry: "enterCompleted",
          exit: "exitCompleted"
        }
      },
      onDone: { target: "Success", actions: "recordDone" }
    },
    Success: { entry: "enterSuccess" }
  }
}, {
  actions: {
    enterWorkflow: marker(workflowTrace, "enterWorkflow"),
    exitWorkflow: marker(workflowTrace, "exitWorkflow"),
    enterWorking: marker(workflowTrace, "enterWorking"),
    exitWorking: marker(workflowTrace, "exitWorking"),
    enterCompleted: marker(workflowTrace, "enterCompleted"),
    exitCompleted: marker(workflowTrace, "exitCompleted"),
    enterSuccess: marker(workflowTrace, "enterSuccess"),
    recordFinish: marker(workflowTrace, "recordFinish"),
    recordDone: marker(workflowTrace, "recordDone")
  }
});
var workflowActor = XFSM.createActor(workflowMachine);
workflowActor.subscribe(function (snapshot) {
  workflowNotifications.push(snapshot.status + ":" +
                             JSON.stringify(snapshot.value));
});
workflowActor.start();
workflowActor.send("finish");
var workflowSnapshot = workflowActor.getSnapshot();
var workflowExpected = [
  "enterWorkflow:xstate.init",
  "enterWorking:xstate.init",
  "exitWorking:finish",
  "recordFinish:finish",
  "enterCompleted:finish",
  "exitCompleted:xstate.done.state.Final state and parent done transition.Workflow",
  "exitWorkflow:xstate.done.state.Final state and parent done transition.Workflow",
  "recordDone:xstate.done.state.Final state and parent done transition.Workflow",
  "enterSuccess:xstate.done.state.Final state and parent done transition.Workflow"
];
var workflowOk = workflowSnapshot.status === "active" &&
  workflowSnapshot.value === "Success" &&
  workflowTrace.join("|") === workflowExpected.join("|") &&
  workflowNotifications.length === 2 &&
  workflowNotifications[1] === "active:\"Success\"";
workflowActor.stop();
workflowMachine = undefined;
workflowActor = undefined;
workflowSnapshot = undefined;
workflowTrace = undefined;
workflowNotifications = undefined;
workflowExpected = undefined;
process.memory();

var targetlessEvents = [];
var targetlessMachine = XFSM.createMachine({
  context: function () { return { count: 0 }; },
  initial: "Parent",
  states: {
    Parent: {
      initial: "Complete",
      states: { Complete: { type: "final" } },
      onDone: {
        actions: [
          "observeDone",
          XFSM.assign({
            count: function (context) { return context.count + 1; }
          })
        ]
      }
    }
  }
}, {
  actions: {
    observeDone: function (context, event) {
      targetlessEvents.push(context.count + ":" + event.type);
    }
  }
});
var targetlessActor = XFSM.createActor(targetlessMachine).start();
var targetlessStarted = targetlessActor.getSnapshot();
targetlessActor.send("MISSING");
var targetlessAfterSend = targetlessActor.getSnapshot();
var targetlessOk = targetlessStarted.status === "active" &&
  targetlessStarted.matches({ Parent: "Complete" }) &&
  targetlessStarted.context.count === 1 &&
  targetlessAfterSend === targetlessStarted &&
  targetlessEvents.join("|") ===
    "0:xstate.done.state.(machine).Parent";
targetlessActor.stop();
targetlessMachine = undefined;
targetlessActor = undefined;
targetlessStarted = undefined;
targetlessAfterSend = undefined;
targetlessEvents = undefined;
process.memory();

var doneTrace = [];
var doneNotices = [];
var doneMachine = XFSM.createMachine({
  entry: "enterRoot",
  exit: "exitRoot",
  initial: "Active",
  states: {
    Active: {
      exit: "exitActive",
      on: { FINISH: { target: "Complete", actions: "finish" } }
    },
    Complete: {
      type: "final",
      entry: "enterComplete",
      exit: "exitComplete"
    }
  }
}, {
  actions: {
    enterRoot: marker(doneTrace, "enterRoot"),
    exitRoot: marker(doneTrace, "exitRoot"),
    exitActive: marker(doneTrace, "exitActive"),
    finish: marker(doneTrace, "finish"),
    enterComplete: marker(doneTrace, "enterComplete"),
    exitComplete: marker(doneTrace, "exitComplete")
  }
});
var doneActor = XFSM.createActor(doneMachine);
var doneSubscription = doneActor.subscribe(function (snapshot) {
  doneNotices.push(snapshot.status);
});
doneActor.start();
doneActor.send("FINISH");
var doneSnapshot = doneActor.getSnapshot();
doneActor.send("IGNORED");
doneSubscription.unsubscribe();
var doneRestartError = "";
try { doneActor.start(); } catch (error) { doneRestartError = "" + error; }
var doneExpected = [
  "enterRoot:xstate.init",
  "exitActive:FINISH",
  "finish:FINISH",
  "enterComplete:FINISH",
  "exitComplete:FINISH",
  "exitRoot:FINISH"
];
var doneOk = doneSnapshot.status === "done" &&
  doneSnapshot.value === "Complete" &&
  doneActor.getSnapshot() === doneSnapshot &&
  doneTrace.join("|") === doneExpected.join("|") &&
  doneNotices.join("|") === "active|done" &&
  doneRestartError.indexOf("E_ACTOR_STATE") >= 0;
doneMachine = undefined;
doneActor = undefined;
doneSnapshot = undefined;
doneSubscription = undefined;
doneTrace = undefined;
doneNotices = undefined;
doneExpected = undefined;
process.memory();

function loopMachine(limit, always) {
  return XFSM.createMachine({
    context: function () { return { count: 0 }; },
    initial: "Idle",
    states: {
      Idle: { on: { GO: "Loop" } },
      Loop: {
        initial: "Complete",
        states: { Complete: { type: "final" } },
        onDone: {
          target: "Loop",
          guard: "again",
          actions: XFSM.assign({
            count: function (context) { return context.count + 1; }
          })
        }
      }
    }
  }, {
    guards: {
      again: function (context) { return always || context.count < limit; }
    }
  });
}

var boundaryActor = XFSM.createActor(loopMachine(255, false)).start();
boundaryActor.send("GO");
var boundarySnapshot = boundaryActor.getSnapshot();
var boundaryOk = boundarySnapshot.status === "active" &&
  boundarySnapshot.matches({ Loop: "Complete" }) &&
  boundarySnapshot.context.count === 255;
boundaryActor.stop();
boundaryActor = undefined;
boundarySnapshot = undefined;
process.memory();

var overflowActor = XFSM.createActor(loopMachine(0, true)).start();
var overflowStable = overflowActor.getSnapshot();
var overflowError = "";
try { overflowActor.send("GO"); }
catch (error) { overflowError = "" + error; }
var overflowSnapshot = overflowActor.getSnapshot();
var overflowOk = overflowError.indexOf(
    "E_MICROSTEP_LIMIT @ actor.send: max=256") >= 0 &&
  overflowSnapshot.status === "error" &&
  overflowSnapshot.value === "Idle" &&
  overflowSnapshot.context === overflowStable.context &&
  overflowSnapshot.context.count === 0;
overflowActor = undefined;
overflowStable = undefined;
overflowSnapshot = undefined;
process.memory();

result = workflowOk && targetlessOk && doneOk && boundaryOk && overflowOk;
print("TEST=xfsm_completion");
print((result ? "PASS " : "FAIL ") + "completion");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
