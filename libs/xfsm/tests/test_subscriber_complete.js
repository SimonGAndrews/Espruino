echo(false);
(function () {
var XFSM = require("XFSM");

function has(error, category) {
  return ("" + error).indexOf(category) >= 0;
}

function capture(fn) {
  try { fn(); }
  catch (error) { return error; }
  return undefined;
}

var machine = XFSM.createMachine({
  initial: "Active",
  states: {
    Active: { on: { FINISH: "Complete" } },
    Complete: { type: "final" }
  }
});

var validationActor = XFSM.createActor(machine);
var listener = function () {};
var invalidListeners = [
  capture(function () { validationActor.subscribe(); }),
  capture(function () { validationActor.subscribe(1); }),
  capture(function () { validationActor.subscribe({ next: listener }); }),
  capture(function () { validationActor.subscribe(listener, undefined); }),
  capture(function () { validationActor.subscribe(listener, listener); })
];
var validAfterRejections = validationActor.start().getSnapshot().status;

var calls = [];
var actor = XFSM.createActor(machine);
var self;
var removed;
var late;
var busy = [];
var doneBusy = [];
var publicationSnapshots = [];
self = actor.subscribe(function (snapshot) {
  calls.push("self:" + snapshot.status);
  publicationSnapshots.push(snapshot === actor.getSnapshot());
  self.unsubscribe();
  removed.unsubscribe();
  late = actor.subscribe(function (laterSnapshot) {
    calls.push("late:" + laterSnapshot.status);
  });
  busy.push(has(capture(function () { actor.start(); }), "E_ACTOR_BUSY"));
  busy.push(has(capture(function () { actor.send("NESTED"); }),
                "E_ACTOR_BUSY"));
  busy.push(has(capture(function () { actor.stop(); }), "E_ACTOR_BUSY"));
});
removed = actor.subscribe(function (snapshot) {
  calls.push("removed:" + snapshot.status);
});
var persistent = actor.subscribe(function (snapshot) {
  calls.push("persistent:" + snapshot.status);
});
var duplicateListener = function (snapshot) {
  calls.push("duplicate:" + snapshot.status);
};
var duplicateA = actor.subscribe(duplicateListener);
var duplicateB = actor.subscribe(duplicateListener);
actor.subscribe(function (snapshot) {
  if (snapshot.status !== "done") return;
  doneBusy.push(has(capture(function () { actor.start(); }),
                    "E_ACTOR_BUSY"));
  doneBusy.push(has(capture(function () { actor.send("NESTED"); }),
                    "E_ACTOR_BUSY"));
  doneBusy.push(has(capture(function () { actor.stop(); }),
                    "E_ACTOR_BUSY"));
});

actor.start();
actor.send("UNHANDLED");
actor.send("FINISH");
var doneSnapshot = actor.getSnapshot();
var doneCalls = calls.join("|");
self.unsubscribe();
removed.unsubscribe();
late.unsubscribe();
persistent.unsubscribe();
duplicateA.unsubscribe();
duplicateB.unsubscribe();

var firstThrown = { listener: 1 };
var secondThrown = { listener: 2 };
var errorCalls = [];
var errorActor = XFSM.createActor(machine);
errorActor.subscribe(function () {
  errorCalls.push("first");
  throw firstThrown;
});
errorActor.subscribe(function () {
  errorCalls.push("second");
  throw secondThrown;
});
errorActor.subscribe(function () { errorCalls.push("third"); });
var observedFirst = capture(function () { errorActor.start(); });
var committedAfterListenerError = errorActor.getSnapshot();
var observedAgain = capture(function () { errorActor.send("UNHANDLED"); });

var stoppedBusy = [];
var stoppedActor = XFSM.createActor(machine);
var stoppedSub = stoppedActor.subscribe(function () {
  stoppedBusy.push(has(capture(function () { stoppedActor.start(); }),
                       "E_ACTOR_BUSY"));
  stoppedBusy.push(has(capture(function () { stoppedActor.send("X"); }),
                       "E_ACTOR_BUSY"));
  stoppedBusy.push(has(capture(function () { stoppedActor.stop(); }),
                       "E_ACTOR_BUSY"));
});
stoppedActor.stop();
stoppedSub.unsubscribe();

var terminalSubscription = actor.subscribe(function () {
  calls.push("terminalWrong");
});
terminalSubscription.unsubscribe();
var borrowedUnsubscribe = capture(function () {
  terminalSubscription.unsubscribe.call({});
});
var detachedUnsubscribe = terminalSubscription.unsubscribe;
var detachedError = capture(function () { detachedUnsubscribe(); });

result = invalidListeners.every(function (error) {
    return has(error, "E_LISTENER_INVALID");
  }) &&
  validAfterRejections === "active" &&
  publicationSnapshots.join("|") === "true" &&
  busy.join("|") === "true|true|true" &&
  doneBusy.join("|") === "true|true|true" &&
  stoppedBusy.join("|") === "true|true|true" &&
  doneSnapshot.status === "done" &&
  doneCalls === [
    "self:active", "persistent:active", "duplicate:active",
    "duplicate:active", "persistent:active", "duplicate:active",
    "duplicate:active", "late:active", "persistent:done",
    "duplicate:done", "duplicate:done", "late:done"
  ].join("|") &&
  calls.join("|") === doneCalls &&
  observedFirst === firstThrown && observedAgain === firstThrown &&
  committedAfterListenerError.status === "active" &&
  errorActor.getSnapshot() === committedAfterListenerError &&
  errorCalls.join("|") === "first|second|third|first|second|third" &&
  has(borrowedUnsubscribe, "E_RECEIVER_INVALID") &&
  has(detachedError, "E_RECEIVER_INVALID");

print("TEST=xfsm_subscriber_complete");
print((result ? "PASS " : "FAIL ") + "subscriber_complete");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
