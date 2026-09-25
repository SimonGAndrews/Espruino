var XFSM = require("XFSM");
var calls = [];
var machine = XFSM.createMachine({
  initial: "Idle",
  states: { Idle: {} }
});
var actor = XFSM.createActor(machine);
var second;
var late;
var first = actor.subscribe(function (snapshot) {
  calls.push("first:" + snapshot.status);
  if (!late) {
    second.unsubscribe();
    late = actor.subscribe(function (laterSnapshot) {
      calls.push("late:" + laterSnapshot.status);
    });
  }
});
second = actor.subscribe(function (snapshot) {
  calls.push("second:" + snapshot.status);
});

actor.start();
actor.send("UNHANDLED");
actor.stop();
first.unsubscribe();
late.unsubscribe();

var stoppedCalls = calls.join("|");
actor.send("IGNORED");

var stoppedBeforeStart = XFSM.createActor(machine);
var stoppedNotice = "";
stoppedBeforeStart.subscribe(function (snapshot) {
  stoppedNotice = snapshot.status + ":" + snapshot.value + ":" +
                  snapshot.context;
});
stoppedBeforeStart.stop();

result = stoppedCalls ===
    "first:active|first:active|late:active|first:stopped|late:stopped" &&
  calls.join("|") === stoppedCalls &&
  stoppedNotice === "stopped:undefined:undefined" &&
  stoppedBeforeStart.getSnapshot().status === "stopped";
