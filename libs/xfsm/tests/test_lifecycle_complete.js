echo(false);
var XFSM = require("XFSM");

function has(error, category) {
  return ("" + error).indexOf(category) >= 0;
}

function capture(fn) {
  try { fn(); }
  catch (error) { return error; }
  return undefined;
}

var basicMachine = XFSM.createMachine({
  initial: "Active",
  states: { Active: {} }
});

var notStarted = XFSM.createActor(basicMachine);
var notStartedSend = capture(function () { notStarted.send("EVENT"); });
var stoppedNotices = [];
notStarted.subscribe(function (snapshot) {
  stoppedNotices.push(snapshot.status);
});
var stoppedReturn = notStarted.stop();
var stoppedSnapshot = notStarted.getSnapshot();
var stoppedSendReturn = notStarted.send("IGNORED");
var stoppedStopReturn = notStarted.stop();
var stoppedStart = capture(function () { notStarted.start(); });

var activeNotices = [];
var active = XFSM.createActor(basicMachine);
active.subscribe(function (snapshot) {
  activeNotices.push(snapshot.status);
});
var firstStartReturn = active.start();
var repeatedStartReturn = active.start();
var unhandledReturn = active.send("UNHANDLED");
var activeStopReturn = active.stop();
var activeStoppedSnapshot = active.getSnapshot();

var finalMachine = XFSM.createMachine({
  initial: "Complete",
  states: { Complete: { type: "final" } }
});
var done = XFSM.createActor(finalMachine);
var doneNotices = [];
var doneSubscription = done.subscribe(function (snapshot) {
  doneNotices.push(snapshot.status);
});
var doneStartReturn = done.start();
var doneSnapshot = done.getSnapshot();
var doneSendReturn = done.send("IGNORED");
var doneStopReturn = done.stop();
var doneRestart = capture(function () { done.start(); });
doneSubscription.unsubscribe();

var basicOk = (
  has(notStartedSend, "E_ACTOR_STATE") &&
  stoppedReturn === notStarted && stoppedStopReturn === notStarted &&
  stoppedSendReturn === undefined && has(stoppedStart, "E_ACTOR_STATE") &&
  stoppedSnapshot.status === "stopped" &&
  stoppedSnapshot.value === undefined && stoppedSnapshot.context === undefined &&
  stoppedNotices.join("|") === "stopped" &&
  firstStartReturn === active && repeatedStartReturn === active &&
  unhandledReturn === undefined && activeStopReturn === active &&
  activeStoppedSnapshot.status === "stopped" &&
  activeNotices.join("|") === "active|active|stopped" &&
  doneStartReturn === done && doneSnapshot.status === "done" &&
  doneSendReturn === undefined && doneStopReturn === done &&
  has(doneRestart, "E_ACTOR_STATE") && doneNotices.join("|") === "done");
basicMachine = undefined;
notStarted = undefined;
stoppedSnapshot = undefined;
stoppedNotices = undefined;
active = undefined;
activeStoppedSnapshot = undefined;
activeNotices = undefined;
finalMachine = undefined;
done = undefined;
doneSnapshot = undefined;
doneSubscription = undefined;
doneNotices = undefined;
process.memory();

var busyActor;
var busyCaught = [];
var busyMachine = XFSM.createMachine({
  initial: "Ready",
  states: {
    Ready: {
      on: {
        CAUGHT: { actions: "caughtBusy" },
        ESCAPE: { actions: "escapeBusy" }
      }
    }
  }
}, {
  actions: {
    caughtBusy: function () {
      busyCaught.push(has(capture(function () { busyActor.start(); }),
                          "E_ACTOR_BUSY"));
      busyCaught.push(has(capture(function () { busyActor.send("NESTED"); }),
                          "E_ACTOR_BUSY"));
      busyCaught.push(has(capture(function () { busyActor.stop(); }),
                          "E_ACTOR_BUSY"));
    },
    escapeBusy: function () { busyActor.start(); }
  }
});
busyActor = XFSM.createActor(busyMachine).start();
busyActor.send("CAUGHT");
var busyStable = busyActor.getSnapshot();
var escapingBusy = capture(function () { busyActor.send("ESCAPE"); });
var busyFault = busyActor.getSnapshot();
var busyOk = (busyCaught.join("|") === "true|true|true" &&
  busyStable.status === "active" && has(escapingBusy, "E_ACTOR_BUSY") &&
  busyFault.status === "error" && busyFault.error === escapingBusy);
busyActor = undefined;
busyMachine = undefined;
busyStable = undefined;
busyFault = undefined;
busyCaught = undefined;
escapingBusy = undefined;
process.memory();

var guardThrown = { stage: "guard" };
var guardAfter = false;
var guardMachine = XFSM.createMachine({
  initial: "Ready",
  states: {
    Ready: {
      on: {
        GO: [
          { target: "Other", guard: "throwGuard" },
          { target: "Other", actions: "afterGuard" }
        ]
      }
    },
    Other: {}
  }
}, {
  guards: { throwGuard: function () { throw guardThrown; } },
  actions: { afterGuard: function () { guardAfter = true; } }
});
var guardActor = XFSM.createActor(guardMachine).start();
var guardStable = guardActor.getSnapshot();
var observedGuard = capture(function () { guardActor.send("GO"); });
var guardFault = guardActor.getSnapshot();
var guardOk = (observedGuard === guardThrown &&
  guardFault.status === "error" && guardFault.error === guardThrown &&
  guardFault.value === guardStable.value &&
  guardFault.context === guardStable.context && !guardAfter);
guardMachine = undefined;
guardActor = undefined;
guardStable = undefined;
guardFault = undefined;
guardThrown = undefined;
observedGuard = undefined;
process.memory();

function transitionFault(stage) {
  var thrown = { stage: stage };
  var trace = [];
  var machine = XFSM.createMachine({
    initial: "Ready",
    states: {
      Ready: {
        exit: stage === "exit" ? "fail" : "exitReady",
        on: { GO: { target: "Other", actions: "transition" } }
      },
      Other: {
        entry: [stage === "entry" ? "fail" : "enterOther", "afterEntry"]
      }
    }
  }, {
    actions: {
      fail: function () { trace.push("fail"); throw thrown; },
      exitReady: function () { trace.push("exit"); },
      transition: function () { trace.push("transition"); },
      enterOther: function () { trace.push("entry"); },
      afterEntry: function () { trace.push("afterEntry"); }
    }
  });
  var actor = XFSM.createActor(machine).start();
  var stable = actor.getSnapshot();
  var observed = capture(function () { actor.send("GO"); });
  var failed = actor.getSnapshot();
  return observed === thrown && failed.status === "error" &&
         failed.error === thrown && failed.value === stable.value &&
         failed.context === stable.context &&
         trace.join("|") === (stage === "exit" ? "fail" :
                              "exit|transition|fail");
}

var transitionOk = transitionFault("exit") && transitionFault("entry");
process.memory();

var stopThrown = { stage: "stop" };
var stopMachine = XFSM.createMachine({
  initial: "Ready",
  states: { Ready: { exit: "failStop" } }
}, {
  actions: { failStop: function () { throw stopThrown; } }
});
var stopActor = XFSM.createActor(stopMachine).start();
var stopStable = stopActor.getSnapshot();
var observedStop = capture(function () { stopActor.stop(); });
var stopFault = stopActor.getSnapshot();
var stopOk = (observedStop === stopThrown && stopFault.status === "error" &&
  stopFault.error === stopThrown && stopFault.value === stopStable.value &&
  stopFault.context === stopStable.context);
stopMachine = undefined;
stopActor = undefined;
stopStable = undefined;
stopFault = undefined;
stopThrown = undefined;
observedStop = undefined;
process.memory();

var startThrown = { stage: "start" };
var startupTail = false;
var startupMachine = XFSM.createMachine({
  initial: "Ready",
  states: { Ready: { entry: ["failStart", "startupTail"] } }
}, {
  actions: {
    failStart: function () { throw startThrown; },
    startupTail: function () { startupTail = true; }
  }
});
var startupActor = XFSM.createActor(startupMachine);
var observedStart = capture(function () { startupActor.start(); });
var startupFault = startupActor.getSnapshot();
var faultStart = capture(function () { startupActor.start(); });
var faultSend = capture(function () { startupActor.send("GO"); });
var faultStop = capture(function () { startupActor.stop(); });
var startOk = (observedStart === startThrown &&
  startupFault.status === "error" && startupFault.error === startThrown &&
  startupFault.value === undefined && startupFault.context === undefined &&
  !startupTail && has(faultStart, "E_ACTOR_FAULTED") &&
  has(faultSend, "E_ACTOR_FAULTED") && has(faultStop, "E_ACTOR_FAULTED"));
startupMachine = undefined;
startupActor = undefined;
startupFault = undefined;
startThrown = undefined;
observedStart = undefined;
faultStart = undefined;
faultSend = undefined;
faultStop = undefined;
process.memory();

var borrowed = XFSM.createActor(XFSM.createMachine({
  initial: "Active",
  states: { Active: {} }
}));
var invalidMethods = [
  capture(function () { borrowed.start.call({}); }),
  capture(function () { borrowed.send.call({}, "GO"); }),
  capture(function () { borrowed.stop.call({}); }),
  capture(function () { borrowed.getSnapshot.call({}); }),
  capture(function () { borrowed.subscribe.call({}, function () {}); })
];
var invalidOk = invalidMethods.every(function (error) {
  return has(error, "E_ACTOR_INVALID");
});
borrowed = undefined;
invalidMethods = undefined;
process.memory();

result = (basicOk && busyOk && guardOk && transitionOk && stopOk && startOk &&
  invalidOk);

print("TEST=xfsm_lifecycle_complete");
print((result ? "PASS " : "FAIL ") + "lifecycle_complete");
print("DONE=" + (result ? "PASS" : "FAIL"));
XFSM = undefined;
