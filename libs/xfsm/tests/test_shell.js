var XFSM = require("XFSM");
var functionTypes = [
  typeof XFSM.assign,
  typeof XFSM.createActor,
  typeof XFSM.createMachine
].join(",");
var message = "";

try {
  XFSM.createMachine({});
} catch (error) {
  message = "" + error;
}

result = functionTypes === "function,function,function" &&
  message.indexOf("Profile 1 implementation not available") >= 0;
