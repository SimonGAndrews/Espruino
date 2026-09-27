echo(false);
(function () {
var XFSM = require("XFSM");

var machine = XFSM.createMachine({
  id: "machine",
  initial: "Start",
  states: {
    Start: {
      on: {
        ESCAPED_DOT: "Heating\\.Mode",
        EXACT_DOT: "Heating.Mode",
        LEADING_DOT: "\\.Hidden",
        LEADING_HASH: "\\#Hidden",
        BACKSLASH: "Back\\\\Slash",
        HIERARCHICAL: "Branch.Leaf",
        EXPLICIT_ID: "#right\\.area.Destination",
        IMPLICIT_ID: "#machine.Branch.Leaf"
      }
    },
    "Heating.Mode": {},
    ".Hidden": {},
    "#Hidden": {},
    "Back\\Slash": {},
    Branch: {
      initial: "Leaf",
      states: { Leaf: {} }
    },
    Right: {
      id: "right.area",
      initial: "Destination",
      states: { Destination: {} }
    }
  }
});

function reaches(event, expected) {
  var actor = XFSM.createActor(machine);
  actor.start();
  actor.send(event);
  return actor.getSnapshot().matches(expected);
}

var descendantMachine = XFSM.createMachine({
  initial: "Parent",
  states: {
    Parent: {
      initial: "Current",
      states: {
        Current: {},
        Nested: {
          initial: "Leaf",
          states: { Leaf: {} }
        }
      },
      on: { DESCEND: ".Nested.Leaf" }
    }
  }
});
var descendantActor = XFSM.createActor(descendantMachine);
descendantActor.start();
descendantActor.send("DESCEND");

var defaultIdMachine = XFSM.createMachine({
  initial: "Start",
  states: {
    Start: { on: { GO: "#(machine).Target" } },
    Target: {}
  }
});
var defaultIdActor = XFSM.createActor(defaultIdMachine);
defaultIdActor.start();
defaultIdActor.send("GO");

result =
  reaches("ESCAPED_DOT", "Heating.Mode") &&
  reaches("EXACT_DOT", "Heating.Mode") &&
  reaches("LEADING_DOT", ".Hidden") &&
  reaches("LEADING_HASH", "#Hidden") &&
  reaches("BACKSLASH", "Back\\Slash") &&
  reaches("HIERARCHICAL", { Branch: "Leaf" }) &&
  reaches("EXPLICIT_ID", { Right: "Destination" }) &&
  reaches("IMPLICIT_ID", { Branch: "Leaf" }) &&
  descendantActor.getSnapshot().matches({ Parent: { Nested: "Leaf" } }) &&
  defaultIdActor.getSnapshot().matches("Target");
print("TEST=xfsm_targets");
print((result ? "PASS " : "FAIL ") + "targets");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
