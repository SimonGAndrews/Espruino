var XFSM = require("XFSM");
var functionTypes = [
  typeof XFSM.assign,
  typeof XFSM.createActor,
  typeof XFSM.createMachine
].join(",");
var message = "";
var machine = XFSM.createMachine({
  initial: "idle",
  states: { idle: {} }
});
var assignment = XFSM.assign({ count: 1 });

try {
  XFSM.createActor(machine);
} catch (error) {
  message = "" + error;
}

result = functionTypes === "function,function,function" &&
  Object.keys(machine).length === 0 &&
  Object.keys(assignment).length === 0 &&
  message.indexOf("Profile 1 actor runtime not available") >= 0;
