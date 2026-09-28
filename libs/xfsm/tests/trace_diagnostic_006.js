/* XFC-CF-DIAG-006: diagnostic categories, paths, types and rollback. */
(function () {
  var XFSM = require("XFSM");

  function has(error, category, path) {
    return error &&
      ("" + error).indexOf("XFC " + category + " @ " + path) >= 0;
  }

  function expect(name, operation, category, path, typeError) {
    var caught;
    try { operation(); } catch (error) { caught = error; }
    if (caught) XFCTrace.error(name, category, path, caught);
    XFCTrace.assert(name,
      has(caught, category, path) &&
      (typeError ? caught instanceof TypeError :
        caught instanceof Error && !(caught instanceof TypeError)));
    return caught;
  }

  function nested(depth) {
    var node = {};
    var index;
    for (index = depth - 1; index >= 1; index--) {
      var children = {};
      children["S" + (index + 1)] = node;
      node = { initial: "S" + (index + 1), states: children };
    }
    var states = {};
    states.S1 = node;
    return { initial: "S1", states: states };
  }

  XFCTrace.begin("XFC-CF-DIAG-006",
    "diagnostic categories, paths, types and rollback", "profile-internal");

  expect("configuration type", function () {
    XFSM.createMachine(null);
  }, "E_CONFIG_TYPE", "config", false);
  expect("unknown property and punctuated path", function () {
    XFSM.createMachine({
      initial: "A.B", states: { "A.B": { unknown: true } }
    });
  }, "E_UNKNOWN_PROPERTY", "config.states[\"A.B\"].unknown", false);
  expect("unsupported feature", function () {
    XFSM.createMachine({ type: "parallel", states: {} });
  }, "E_UNSUPPORTED_FEATURE", "config.type", false);
  expect("initial required", function () {
    XFSM.createMachine({ states: { A: {}, B: {} } });
  }, "E_INITIAL_REQUIRED", "config.initial", false);
  expect("initial unknown", function () {
    XFSM.createMachine({ initial: "missing", states: { A: {} } });
  }, "E_INITIAL_UNKNOWN", "config.initial", false);
  expect("target unknown", function () {
    XFSM.createMachine({
      initial: "A", states: { A: { on: { GO: "missing" } } }
    });
  }, "E_TARGET_UNKNOWN", "config.states.A.on.GO", false);
  expect("target ambiguous", function () {
    XFSM.createMachine({
      initial: "Source",
      states: {
        Source: {
          initial: "Idle",
          states: {
            Idle: {}, "A.B": {}, A: { initial: "B", states: { B: {} } }
          },
          on: { GO: ".A.B" }
        }
      }
    });
  }, "E_TARGET_AMBIGUOUS", "config.states.Source.on.GO", false);
  expect("duplicate id", function () {
    XFSM.createMachine({
      initial: "A", states: { A: { id: "same" }, B: { id: "same" } }
    });
  }, "E_ID_DUPLICATE", "config.states.B.id", false);
  expect("unresolved action", function () {
    XFSM.createMachine({ initial: "A", states: { A: { entry: "missing" } } });
  }, "E_ACTION_UNRESOLVED", "config.states.A.entry", false);
  expect("unresolved guard", function () {
    XFSM.createMachine({
      initial: "A", states: { A: { on: { GO: { guard: "missing" } } } }
    });
  }, "E_GUARD_UNRESOLVED", "config.states.A.on.GO.guard", false);
  expect("representation limit", function () {
    XFSM.createMachine(nested(33));
  }, "E_LIMIT_EXCEEDED", "config.states.S1", false);
  expect("array path and first error", function () {
    XFSM.createMachine({
      initial: "A", states: { A: { entry: [1, 2] } }
    });
  }, "E_CONFIG_TYPE", "config.states.A.entry[0]", false);

  var longTarget = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  var detailError = expect("bounded quoted detail", function () {
    XFSM.createMachine({
      initial: "A", states: { A: { on: { GO: longTarget } } }
    });
  }, "E_TARGET_UNKNOWN", "config.states.A.on.GO", false);
  var detailMessage = "" + detailError;
  var detail = detailMessage.substring(detailMessage.lastIndexOf(": ") + 2);
  XFCTrace.assert("detail is quoted and bounded to 48 bytes",
    detail === "\"" + longTarget.substring(0, 48) + "\"",
    detail.length, 50);
  var escapedError = expect("escaped string detail", function () {
    XFSM.createMachine({
      initial: "A", states: { A: { on: { GO: "quote\"slash\\end" } } }
    });
  }, "E_TARGET_UNKNOWN", "config.states.A.on.GO", false);
  var escapedMessage = "" + escapedError;
  var escapedDetail = escapedMessage.substring(escapedMessage.lastIndexOf(": ") + 2);
  XFCTrace.assert("string detail escapes quote and backslash",
    escapedDetail === "\"quote\\\"slash\\\\end\"");

  var machine = XFSM.createMachine({ initial: "A", states: { A: {} } });
  expect("invalid machine", function () {
    XFSM.createActor({});
  }, "E_MACHINE_INVALID", "createActor.machine", true);
  expect("unsupported actor options", function () {
    XFSM.createActor(machine, {});
  }, "E_UNSUPPORTED_FEATURE", "createActor.options", false);

  var actor = XFSM.createActor(machine);
  expect("invalid actor receiver", function () {
    actor.start.call({});
  }, "E_ACTOR_INVALID", "actor.start.this", true);
  expect("invalid lifecycle state", function () {
    actor.send("GO");
  }, "E_ACTOR_STATE", "actor.send", false);
  XFCTrace.assert("boundary lifecycle error leaves actor usable",
    actor.getSnapshot().status === "notStarted");
  actor.start();
  var stable = actor.getSnapshot();
  expect("invalid event", function () {
    actor.send({ payload: 1 });
  }, "E_EVENT_INVALID", "actor.send.event", true);
  expect("invalid listener", function () {
    actor.subscribe({});
  }, "E_LISTENER_INVALID", "actor.subscribe.listener", true);
  XFCTrace.assert("boundary input errors preserve stable snapshot",
    actor.getSnapshot() === stable);
  expect("invalid snapshot receiver", function () {
    stable.matches.call({}, "A");
  }, "E_RECEIVER_INVALID", "snapshot.matches.this", true);
  var subscription = actor.subscribe(function () {});
  expect("invalid subscription receiver", function () {
    subscription.unsubscribe.call({});
  }, "E_RECEIVER_INVALID", "subscription.unsubscribe.this", true);
  subscription.unsubscribe();

  var busyError;
  var busyActor;
  busyActor = XFSM.createActor(XFSM.createMachine({
    initial: "A",
    states: { A: { on: { GO: { actions: function () {
      try { busyActor.send("NESTED"); } catch (error) { busyError = error; }
    } } } } }
  })).start();
  busyActor.send("GO");
  if (busyError) XFCTrace.error("busy actor", "E_ACTOR_BUSY", "actor.send", busyError);
  XFCTrace.assert("caught busy rejection does not fault actor",
    has(busyError, "E_ACTOR_BUSY", "actor.send") &&
    busyError instanceof Error && !(busyError instanceof TypeError) &&
    busyActor.getSnapshot().status === "active");

  var thrown = { code: 17 };
  var faultActor = XFSM.createActor(XFSM.createMachine({
    initial: "A",
    states: { A: { on: { FAIL: { actions: function () { throw thrown; } } } } }
  })).start();
  var observed;
  try { faultActor.send("FAIL"); } catch (error) { observed = error; }
  XFCTrace.assert("application throw identity retained",
    observed === thrown && faultActor.getSnapshot().error === thrown);
  expect("faulted actor", function () {
    faultActor.send("GO");
  }, "E_ACTOR_FAULTED", "actor.send", false);

  var contextActor = XFSM.createActor(XFSM.createMachine({
    context: function () { return 1; }, initial: "A", states: { A: {} }
  }));
  var contextError = expect("invalid runtime context", function () {
    contextActor.start();
  }, "E_CONTEXT_INVALID", "actor.start.context", true);
  var contextSnapshot = contextActor.getSnapshot();
  XFCTrace.assert("runtime context error faults before publication",
    contextSnapshot.status === "error" &&
    contextSnapshot.error === contextError &&
    contextSnapshot.value === undefined && contextSnapshot.context === undefined);

  XFCTrace.finish();
})();
