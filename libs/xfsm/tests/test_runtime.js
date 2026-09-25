var XFSM = require("XFSM");
var trace = [];
var notifications = [];

function mark(name) {
  return function (context, event) {
    trace.push(name + ":" + context.count + ":" + event.type);
  };
}

var machine = XFSM.createMachine({
  context: function () { return { count: 0 }; },
  initial: "Parent",
  states: {
    Parent: {
      initial: "ChildA",
      entry: "enterParent",
      exit: "exitParent",
      states: {
        ChildA: {
          entry: "enterA",
          exit: "exitA",
          on: {
            GO: [
              { target: "ChildB", guard: "never", actions: "wrong" },
              {
                target: "ChildB",
                guard: "ready",
                actions: [
                  "before",
                  XFSM.assign({
                    count: function (context) { return context.count + 1; }
                  }),
                  "after"
                ]
              }
            ]
          }
        },
        ChildB: {
          entry: "enterB",
          exit: "exitB",
          on: {
            PING: { actions: "ping" },
            AGAIN: { target: "ChildB", actions: "again" },
            RESTART: {
              target: "ChildB",
              reenter: true,
              actions: "restart"
            }
          }
        }
      },
      on: {
        OUT: { target: "#outside", actions: "out" }
      }
    },
    Outside: {
      id: "outside",
      entry: "enterOutside",
      exit: "exitOutside",
      on: {
        BACK: { target: "Parent", actions: "back" }
      }
    }
  },
  id: "machine"
}, {
  actions: {
    enterParent: mark("enterParent"),
    exitParent: mark("exitParent"),
    enterA: mark("enterA"),
    exitA: mark("exitA"),
    enterB: mark("enterB"),
    exitB: mark("exitB"),
    enterOutside: mark("enterOutside"),
    exitOutside: mark("exitOutside"),
    before: mark("before"),
    after: mark("after"),
    wrong: mark("wrong"),
    ping: mark("ping"),
    again: mark("again"),
    restart: mark("restart"),
    out: mark("out"),
    back: mark("back")
  },
  guards: {
    never: function () { trace.push("guardNever"); return false; },
    ready: function (context, event) {
      trace.push("guardReady:" + context.count + ":" + event.type);
      return true;
    }
  },
  actors: {}, services: {}, delays: {}
});

var actor = XFSM.createActor(machine);
var firstSubscription = actor.subscribe(function (snapshot) {
  notifications.push(snapshot.status + ":" + JSON.stringify(snapshot.value) +
                     ":" + (snapshot.context && snapshot.context.count));
});
var before = actor.getSnapshot();
var sameBefore = before === actor.getSnapshot();

actor.start();
var started = actor.getSnapshot();
var matchesParent = started.matches("Parent") &&
                    started.matches({ Parent: "ChildA" });
actor.send({ type: "GO", payload: 42 });
var moved = actor.getSnapshot();
var movedIdentity = moved !== started;
var movedMatches = moved.matches({ Parent: "ChildB" });
actor.send("PING");
var pinged = actor.getSnapshot();
var targetlessIdentity = pinged === moved;
actor.send("AGAIN");
var selfIdentity = actor.getSnapshot() === pinged;
actor.send("RESTART");
var reenterIdentity = actor.getSnapshot() === pinged;
actor.send("OUT");
var outside = actor.getSnapshot();
actor.send("BACK");
var returned = actor.getSnapshot();
firstSubscription.unsubscribe();
actor.stop();
var stopped = actor.getSnapshot();

var expectedTrace = [
  "enterParent:0:xstate.init",
  "enterA:0:xstate.init",
  "guardNever",
  "guardReady:0:GO",
  "exitA:0:GO",
  "before:0:GO",
  "after:1:GO",
  "enterB:1:GO",
  "ping:1:PING",
  "again:1:AGAIN",
  "exitB:1:RESTART",
  "restart:1:RESTART",
  "enterB:1:RESTART",
  "exitB:1:OUT",
  "exitParent:1:OUT",
  "out:1:OUT",
  "enterOutside:1:OUT",
  "exitOutside:1:BACK",
  "back:1:BACK",
  "enterParent:1:BACK",
  "enterA:1:BACK",
  "exitA:1:xstate.stop",
  "exitParent:1:xstate.stop"
];

result = sameBefore &&
  before.status === "notStarted" && before.value === undefined &&
  started.status === "active" && matchesParent &&
  movedIdentity && moved.context.count === 1 && movedMatches &&
  targetlessIdentity && selfIdentity && reenterIdentity &&
  outside.value === "Outside" &&
  returned.matches({ Parent: "ChildA" }) &&
  stopped.status === "stopped" &&
  stopped.matches({ Parent: "ChildA" }) &&
  trace.join("|") === expectedTrace.join("|") &&
  notifications.length === 7;
