/* XFC-CF-SNAP-002: complete snapshot, matches, cache and lazy contract. */
(function () {
  var XFSM = require("XFSM");
  XFCTrace.begin("XFC-CF-SNAP-002",
    "snapshot values, matching, identity and lazy publication", "profile-internal");

  var rootActor = XFSM.createActor(XFSM.createMachine({}));
  var rootBefore = rootActor.getSnapshot();
  XFCTrace.assert("not-started snapshot", rootBefore.status === "notStarted" &&
    rootBefore.value === undefined && rootBefore.context === undefined &&
    !rootBefore.matches({}));
  rootActor.start();
  var rootSnapshot = rootActor.getSnapshot();
  XFCTrace.assert("atomic root empty value", rootSnapshot.status === "active" &&
    Object.keys(rootSnapshot.value).length === 0 && rootSnapshot.matches({}) &&
    !rootSnapshot.matches("anything") && !rootSnapshot.matches({ A: "B" }));

  var machine = XFSM.createMachine({
    context: { count: 0 },
    initial: "Parent.With.Dot",
    states: {
      "Parent.With.Dot": {
        initial: "Child",
        states: {
          Child: {
            on: {
              NOOP: { actions: function () {} },
              ASSIGN: { actions: XFSM.assign({ count: 1 }) },
              NEXT: "Grand"
            }
          },
          Grand: {
            initial: "Leaf",
            states: { Leaf: {} }
          }
        }
      }
    }
  });
  var actor = XFSM.createActor(machine);
  XFCTrace.assert("unobserved actor has no cached snapshot",
    actor["\xFFxfaS"] === undefined);
  actor.start();
  XFCTrace.assert("start without observer remains lazy",
    actor["\xFFxfaS"] === undefined);
  var first = actor.getSnapshot();
  var firstValue = JSON.stringify(first.value);
  XFCTrace.snapshot("nested active", first, first.context.count);
  XFCTrace.assert("hierarchical partial matches",
    first.matches("Parent.With.Dot") &&
    first.matches({ "Parent.With.Dot": "Child" }) &&
    !first.matches("Parent") && !first.matches("Parent.With.Dot.Child"));

  var reads = 0;
  var accessor = {};
  Object.defineProperty(accessor, "Parent.With.Dot", {
    enumerable: true,
    get: function () { reads++; return "Child"; }
  });
  var malformed = [undefined, "", null, [], function () {}, 1, true,
    {}, { A: "B", C: "D" }, { "Parent.With.Dot": {} },
    { "Parent.With.Dot": 1 }, { "Parent.With.Dot": true },
    { "Parent.With.Dot": undefined }, accessor];
  var malformedFalse = true;
  var malformedResults = [];
  for (var index = 0; index < malformed.length; index++) {
    var malformedResult = first.matches(malformed[index]);
    malformedResults.push(malformedResult);
    if (malformedResult) malformedFalse = false;
  }
  var extraArgumentResult = first.matches(
    { "Parent.With.Dot": "Child" }, "ignored");
  XFCTrace.context("matches observations", {
    malformed: malformedResults,
    accessorReads: reads,
    extraArgument: extraArgumentResult
  });
  XFCTrace.assert("malformed matches are total false",
    malformedFalse && reads === 0 &&
    extraArgumentResult);

  actor.send("UNHANDLED");
  XFCTrace.assert("unhandled event reuses snapshot", actor.getSnapshot() === first);
  actor.send("NOOP");
  XFCTrace.assert("action-only event reuses snapshot", actor.getSnapshot() === first);
  actor.send("ASSIGN");
  var assigned = actor.getSnapshot();
  XFCTrace.assert("context change invalidates snapshot",
    assigned !== first && assigned.context.count === 1 &&
    first.context.count === 0 && JSON.stringify(first.value) === firstValue);
  actor.send("NEXT");
  var nested = actor.getSnapshot();
  XFCTrace.assert("state change invalidates with deeper shape",
    nested !== assigned &&
    JSON.stringify(nested.value) ===
      '{"Parent.With.Dot":{"Grand":"Leaf"}}' &&
    nested.matches("Parent.With.Dot") &&
    nested.matches({ "Parent.With.Dot": "Grand" }) &&
    nested.matches({ "Parent.With.Dot": { Grand: "Leaf" } }) &&
    !nested.matches({ "Parent.With.Dot": { Grand: "Other" } }));
  actor.stop();
  var stopped = actor.getSnapshot();
  XFCTrace.assert("stopped snapshot retains matching state",
    stopped.status === "stopped" && stopped.matches("Parent.With.Dot") &&
    stopped.matches({ "Parent.With.Dot": { Grand: "Leaf" } }));

  var doneActor = XFSM.createActor(XFSM.createMachine({
    initial: "Done", states: { Done: { type: "final" } }
  })).start();
  var done = doneActor.getSnapshot();
  XFCTrace.assert("done snapshot retains final state",
    done.status === "done" && done.value === "Done" && done.matches("Done"));

  var thrown = { fault: true };
  var faultActor = XFSM.createActor(XFSM.createMachine({
    initial: "Ready",
    states: { Ready: { on: { FAIL: { actions: function () { throw thrown; } } } } }
  })).start();
  var stable = faultActor.getSnapshot();
  var observed;
  try { faultActor.send("FAIL"); } catch (error) { observed = error; }
  var fault = faultActor.getSnapshot();
  XFCTrace.assert("error snapshot retains stable state and exact error",
    observed === thrown && fault.status === "error" && fault.error === thrown &&
    fault.value === stable.value && fault.context === stable.context &&
    fault.matches("Ready"));

  XFCTrace.finish();
})();
