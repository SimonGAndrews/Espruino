echo(false);
(function () {
var XFSM = require("XFSM");
var locationTrace = [];

function increment(context) {
  return { n: context.n + 1 };
}
function mark(name) {
  return function (context, event) {
    locationTrace.push(name + ":" + context.n + ":" + event.type);
  };
}

var inc = XFSM.assign(increment);
var locationMachine = XFSM.createMachine({
  context: { n: 0 },
  initial: "Parent",
  states: {
    Parent: {
      entry: [inc, "parentEntry"],
      exit: [inc, "parentExit"],
      initial: {
        target: "Idle",
        actions: [inc, "initialTransition"]
      },
      states: {
        Idle: {
          entry: [inc, "idleEntry"],
          exit: [inc, "idleExit"],
          on: {
            GO: {
              target: "Complete",
              actions: [inc, "eventTransition"]
            }
          }
        },
        Complete: {
          type: "final",
          entry: [inc, "completeEntry"],
          exit: [inc, "completeExit"]
        }
      },
      onDone: {
        target: "Outside",
        actions: [inc, "completionTransition"]
      }
    },
    Outside: { entry: [inc, "outsideEntry"] }
  }
}, {
  actions: {
    parentEntry: mark("parentEntry"),
    parentExit: mark("parentExit"),
    initialTransition: mark("initialTransition"),
    idleEntry: mark("idleEntry"),
    idleExit: mark("idleExit"),
    eventTransition: mark("eventTransition"),
    completeEntry: mark("completeEntry"),
    completeExit: mark("completeExit"),
    completionTransition: mark("completionTransition"),
    outsideEntry: mark("outsideEntry")
  }
});
var locationActor = XFSM.createActor(locationMachine).start();
locationActor.send({ type: "GO", payload: 7 });
var locationSnapshot = locationActor.getSnapshot();
var locationExpected = [
  "parentEntry:1:xstate.init",
  "initialTransition:2:xstate.init",
  "idleEntry:3:xstate.init",
  "idleExit:4:GO",
  "eventTransition:5:GO",
  "completeEntry:6:GO",
  "completeExit:7:xstate.done.state.(machine).Parent",
  "parentExit:8:xstate.done.state.(machine).Parent",
  "completionTransition:9:xstate.done.state.(machine).Parent",
  "outsideEntry:10:xstate.done.state.(machine).Parent"
];
var locationsOk = locationSnapshot.value === "Outside" &&
  locationSnapshot.context.n === 10 &&
  locationTrace.join("|") === locationExpected.join("|");

var fixedObject = { retained: true };
var fixedArray = [3, 4];
var replacementObject = { retained: false };
var storedFunction = function () { return "context data"; };
var map = {
  count: function (context) { return context.count + 1; },
  oldCount: function (context) { return context.count; },
  payload: function (context, event) { return event.payload; },
  fixedObject: fixedObject,
  fixedArray: fixedArray
};
var empty = XFSM.assign({});
var identities = [];
var formMachine = XFSM.createMachine({
  context: { count: 1 },
  initial: "Active",
  states: {
    Active: {
      on: {
        UPDATE: {
          actions: [
            "capture",
            "ignoredReturn",
            XFSM.assign(function (context, event) {
              return {
                count: context.count + 1,
                fromPartial: event.payload,
                storedFunction: storedFunction
              };
            }),
            "capture",
            XFSM.assign(map),
            "capture",
            empty,
            "capture"
          ]
        }
      }
    }
  }
}, {
  actions: {
    capture: function (context) { identities.push(context); },
    ignoredReturn: function () { return { count: 999 }; }
  }
});
map.count = function () { return 1000; };
map.fixedObject = replacementObject;
map.late = "not compiled";
var formActor = XFSM.createActor(formMachine).start();
var originalContext = formActor.getSnapshot().context;
formActor.send({ type: "UPDATE", payload: 42 });
var assigned = formActor.getSnapshot().context;
var formsOk = identities.length === 4 &&
  identities[0] === originalContext && identities[0].count === 1 &&
  identities[1] !== identities[0] && identities[1].count === 2 &&
  identities[1].fromPartial === 42 &&
  identities[1].storedFunction === storedFunction &&
  identities[2] !== identities[1] && identities[2].count === 3 &&
  identities[2].oldCount === 2 && identities[2].payload === 42 &&
  identities[2].fixedObject === fixedObject &&
  identities[2].fixedArray === fixedArray &&
  identities[2].late === undefined &&
  identities[3] !== identities[2] &&
  identities[3].count === 3 && assigned === identities[3] &&
  originalContext.count === 1;

result = locationsOk && formsOk;
if (!result)
  print("CHECKS=" + locationsOk + "," + formsOk +
        " TRACE=" + locationTrace.join("|"));
print("TEST=xfsm_assign_forms");
print((result ? "PASS " : "FAIL ") + "assign_forms");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
