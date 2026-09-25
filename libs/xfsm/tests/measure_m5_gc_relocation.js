/* M5 on-device GC and relocation check. */
echo(false);
print("TEST=xfsm_m5_gc_relocation");

var XFSM = require("XFSM");
var calls = 0;
var machine = XFSM.createMachine({
  context: { count: 0 },
  initial: "Parent",
  states: {
    Parent: {
      initial: "Leaf",
      states: {
        Leaf: {
          on: { LOCAL: { actions: "record" } }
        }
      }
    }
  }
}, {
  actions: {
    record: function () { calls++; }
  }
});
var actor = XFSM.createActor(machine).start();

setTimeout(function () {
  process.memory();
  E.defrag();
  actor.send("LOCAL");
  var snapshot = actor.getSnapshot();
  result = calls === 1 && snapshot.status === "active" &&
    snapshot.matches({ Parent: "Leaf" });
  print((result ? "PASS " : "FAIL ") + "gc_relocation_dispatch_snapshot");
  print("DONE=" + (result ? "PASS" : "FAIL"));
  actor.stop();
}, 50);
