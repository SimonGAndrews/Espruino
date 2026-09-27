echo(false);
(function () {
var XFSM = require("XFSM");
var trace = [];

function mark(name) {
  return function () { trace.push(name); };
}

var eventMachine = XFSM.createMachine({
  predictableActionArguments: true,
  preserveActionOrder: true,
  initial: "Parent",
  states: {
    Parent: {
      initial: "Child",
      states: {
        Child: {
          on: {
            "*": { guard: "notFall", actions: "childWildcard" },
            GO: { guard: "never", actions: "wrong" },
            costarring: { actions: "wrong" },
            BLOCK: [
              { guard: "never", actions: "wrong" },
              {}
            ],
            STOP: {},
            STOP_UNDEFINED: undefined,
            STOP_TARGET_UNDEFINED: { target: undefined }
          }
        }
      },
      on: {
        FALL: { actions: "parentFall" },
        "*": { actions: "parentWildcard" }
      }
    }
  }
}, {
  actions: {
    childWildcard: mark("childWildcard"),
    parentFall: mark("parentFall"),
    parentWildcard: mark("parentWildcard"),
    wrong: mark("wrong")
  },
  guards: {
    never: function () { trace.push("never"); return false; },
    notFall: function (context, event) {
      trace.push("notFall:" + event.type);
      return event.type !== "FALL";
    }
  },
  services: {}, actors: {}, delays: {}
});
var eventActor = XFSM.createActor(eventMachine);
eventActor.start();
eventActor.send("GO");
eventActor.send("FALL");
eventActor.send("OTHER");
eventActor.send("liquid");
eventActor.send("BLOCK");
eventActor.send("STOP");
eventActor.send("STOP_UNDEFINED");
eventActor.send("STOP_TARGET_UNDEFINED");

var condMachine = XFSM.createMachine({
  initial: "A",
  states: {
    A: { on: { GO: { target: "B", cond: "allowed" } } },
    B: {}
  }
}, { guards: { allowed: function () { return true; } } });
var condActor = XFSM.createActor(condMachine);
condActor.start();
condActor.send("GO");

var selfTrace = [];
var selfMachine = XFSM.createMachine({
  initial: "Active",
  states: {
    Active: {
      entry: function () { selfTrace.push("entry"); },
      exit: function () { selfTrace.push("exit"); },
      on: {
        REENTER: {
          target: "Active",
          internal: false,
          actions: function () { selfTrace.push("external"); }
        },
        PRESERVE: {
          target: "Active",
          internal: true,
          actions: function () { selfTrace.push("internal"); }
        }
      }
    }
  }
});
var selfActor = XFSM.createActor(selfMachine);
selfActor.start();
selfTrace = [];
selfActor.send("REENTER");
selfActor.send("PRESERVE");

result =
  trace.join("|") ===
    "never|notFall:GO|childWildcard|notFall:FALL|parentFall|" +
    "notFall:OTHER|childWildcard|notFall:liquid|childWildcard|never" &&
  eventActor.getSnapshot().matches({ Parent: "Child" }) &&
  condActor.getSnapshot().matches("B") &&
  selfTrace.join("|") === "exit|external|entry|internal";
print("TEST=xfsm_events_migration");
print((result ? "PASS " : "FAIL ") + "events_migration");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
