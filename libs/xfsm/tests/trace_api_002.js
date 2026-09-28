/* XFC-CF-API-002: negative public surface and private-brand rejection. */
(function () {
  var XFSM = require("XFSM");

  function errorMatches(error, category, path, type) {
    return error && error instanceof type &&
      ("" + error).indexOf("XFC " + category + " @ " + path) >= 0;
  }

  function expectError(name, operation, category, path, type) {
    var caught;
    try {
      operation();
    } catch (error) {
      caught = error;
    }
    if (caught) XFCTrace.error(name, category, path, caught);
    XFCTrace.assert(name, errorMatches(caught, category, path, type));
  }

  XFCTrace.begin("XFC-CF-API-002",
    "negative public surface and private-brand rejection", "profile");

  XFCTrace.assert("module factories", [
    typeof XFSM.createMachine,
    typeof XFSM.createActor,
    typeof XFSM.assign
  ].join(",") === "function,function,function");
  XFCTrace.assert("legacy interpret absent", typeof XFSM.interpret === "undefined");
  XFCTrace.assert("private test hooks absent",
    typeof XFSM._measure === "undefined" &&
    typeof XFSM._failNext === "undefined");

  var machine = XFSM.createMachine({
    initial: "Idle",
    states: { Idle: {} }
  });
  var actor = XFSM.createActor(machine);
  var assignment = XFSM.assign({ count: 1 });
  var snapshot = actor.getSnapshot();
  var subscription = actor.subscribe(function () {});

  XFCTrace.assert("machine is opaque", Object.keys(machine).length === 0 &&
    typeof machine.provide === "undefined" &&
    typeof machine.withConfig === "undefined" &&
    typeof machine.transition === "undefined" &&
    typeof machine.getInitialSnapshot === "undefined" &&
    typeof machine.getStateNodeById === "undefined");
  XFCTrace.assert("actor methods are shared", Object.keys(actor).length === 0 &&
    typeof actor.start === "function" && typeof actor.send === "function" &&
    typeof actor.stop === "function" &&
    typeof actor.getSnapshot === "function" &&
    typeof actor.subscribe === "function" &&
    typeof actor.onTransition === "undefined");
  XFCTrace.assert("assignment descriptor is opaque",
    Object.keys(assignment).length === 0);
  XFCTrace.assert("snapshot has no action list",
    !("actions" in snapshot) && typeof snapshot.matches === "function");
  XFCTrace.assert("subscription method is shared",
    Object.keys(subscription).length === 0 &&
    typeof subscription.unsubscribe === "function");

  expectError("ordinary machine rejected", function () {
    XFSM.createActor({});
  }, "E_MACHINE_INVALID", "createActor.machine", TypeError);
  expectError("cloned machine rejected", function () {
    XFSM.createActor(Object.assign({}, machine));
  }, "E_MACHINE_INVALID", "createActor.machine", TypeError);
  expectError("assignment is not machine", function () {
    XFSM.createActor(assignment);
  }, "E_MACHINE_INVALID", "createActor.machine", TypeError);
  expectError("actor receiver rejected", function () {
    actor.start.call({});
  }, "E_ACTOR_INVALID", "actor.start", TypeError);
  expectError("snapshot receiver rejected", function () {
    snapshot.matches.call({}, "Idle");
  }, "E_RECEIVER_INVALID", "snapshot.matches", TypeError);
  expectError("subscription receiver rejected", function () {
    subscription.unsubscribe.call({});
  }, "E_RECEIVER_INVALID", "subscription.unsubscribe", TypeError);

  var forgedAssignment = {};
  forgedAssignment["\xFFxfcB"] = "XFAD1";
  forgedAssignment["\xFFxfcV"] = { count: 2 };
  expectError("forged assignment rejected", function () {
    XFSM.createMachine({
      initial: "Idle",
      states: { Idle: { entry: forgedAssignment } }
    });
  }, "E_CONFIG_TYPE", "config.states.Idle.entry", Error);

  subscription.unsubscribe();
  XFCTrace.finish();
})();
