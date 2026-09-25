var XFSM = require("XFSM");
var functionTypes = [
  typeof XFSM.assign,
  typeof XFSM.createActor,
  typeof XFSM.createMachine
].join(",");
var machine = XFSM.createMachine({
  initial: "idle",
  states: { idle: {} }
});
var assignment = XFSM.assign({ count: 1 });
var actor = XFSM.createActor(machine);
var before = actor.getSnapshot();
actor.start();
var after = actor.getSnapshot();

result = functionTypes === "function,function,function" &&
  Object.keys(machine).length === 0 &&
  Object.keys(assignment).length === 0 &&
  Object.keys(actor).length === 0 &&
  before.status === "notStarted" &&
  before.value === undefined &&
  before.context === undefined &&
  after.status === "active" &&
  after.value === "idle" &&
  after.context !== undefined &&
  after.matches("idle") &&
  !after.matches("other");
