/* Build with XFC_STACK_RESERVE=2000000 to exercise the pre-operation guard. */
var XFSM = require("XFSM");
var machine = XFSM.createMachine({
  initial: "Idle",
  states: { Idle: {} }
});
var actor = XFSM.createActor(machine);
var message = "";

try {
  actor.start();
} catch (error) {
  message = String(error);
}

result =
  message.indexOf("XFC E_LIMIT_EXCEEDED @ actor.start: stack") >= 0 &&
  actor.getSnapshot().status === "notStarted";
