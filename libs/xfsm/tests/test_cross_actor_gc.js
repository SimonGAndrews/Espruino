echo(false);
var XFSM = require("XFSM");

function capture(fn) {
  try { fn(); }
  catch (error) { return error; }
  return undefined;
}

var nestedTrace = [];
var incrementB = XFSM.assign({
  count: function (context) { return context.count + 1; }
});
var bMachine = XFSM.createMachine({
  context: { count: 0 },
  initial: "Ready",
  states: {
    Ready: {
      on: {
        STEP: {
          target: "Stepped",
          actions: [incrementB, "recordStep"]
        }
      }
    },
    Stepped: { on: { RESET: "Ready" } }
  }
}, {
  actions: {
    recordStep: function () { nestedTrace.push("bStep"); }
  }
});
var bActor = XFSM.createActor(bMachine).start();

function resetB() {
  if (bActor.getSnapshot().matches("Stepped")) bActor.send("RESET");
}

var nestedAssign = XFSM.assign(function () {
  nestedTrace.push("assignBefore");
  bActor.send("STEP");
  nestedTrace.push("assignAfter");
  return { observed: bActor.getSnapshot().context.count };
});
var aMachine = XFSM.createMachine({
  context: { observed: 0 },
  initial: "Idle",
  states: {
    Idle: {
      on: {
        GUARD: { target: "Done", guard: "nestedGuard" },
        ASSIGN: { target: "Done", actions: nestedAssign },
        ACTION: { target: "Done", actions: "nestedAction" },
        OUTER_FAULT: {
          target: "Done",
          actions: ["nestedAction", "throwOuter"]
        }
      }
    },
    Done: {}
  }
}, {
  guards: {
    nestedGuard: function () {
      nestedTrace.push("guardBefore");
      bActor.send("STEP");
      nestedTrace.push("guardAfter");
      return true;
    }
  },
  actions: {
    nestedAction: function () {
      nestedTrace.push("actionBefore");
      bActor.send("STEP");
      nestedTrace.push("actionAfter");
    },
    throwOuter: function () { throw outerThrown; }
  }
});

var guardActor = XFSM.createActor(aMachine).start();
nestedTrace = [];
guardActor.send("GUARD");
var guardOk = (guardActor.getSnapshot().matches("Done") &&
  bActor.getSnapshot().matches("Stepped") &&
  bActor.getSnapshot().context.count === 1 &&
  nestedTrace.join("|") === "guardBefore|bStep|guardAfter");
guardActor.stop();
guardActor = undefined;
resetB();
process.memory();

var assignActor = XFSM.createActor(aMachine).start();
nestedTrace = [];
assignActor.send("ASSIGN");
var assignOk = (assignActor.getSnapshot().matches("Done") &&
  assignActor.getSnapshot().context.observed === 2 &&
  bActor.getSnapshot().matches("Stepped") &&
  nestedTrace.join("|") === "assignBefore|bStep|assignAfter");
assignActor.stop();
assignActor = undefined;
resetB();
process.memory();

var actionActor = XFSM.createActor(aMachine).start();
nestedTrace = [];
actionActor.send("ACTION");
var actionOk = (actionActor.getSnapshot().matches("Done") &&
  bActor.getSnapshot().context.count === 3 &&
  nestedTrace.join("|") === "actionBefore|bStep|actionAfter");
actionActor.stop();
actionActor = undefined;
resetB();
process.memory();

var listenerActor = XFSM.createActor(XFSM.createMachine({
  initial: "Idle",
  states: { Idle: {} }
}));
var listenerSubscription = listenerActor.subscribe(function () {
  nestedTrace.push("listenerBefore");
  bActor.send("STEP");
  nestedTrace.push("listenerAfter");
});
nestedTrace = [];
listenerActor.start();
var listenerOk = (bActor.getSnapshot().context.count === 4 &&
  nestedTrace.join("|") === "listenerBefore|bStep|listenerAfter");
listenerSubscription.unsubscribe();
listenerActor.stop();
listenerSubscription = undefined;
listenerActor = undefined;
resetB();
process.memory();

var outerThrown = { source: "outer" };
var outerActor = XFSM.createActor(aMachine).start();
var outerStable = outerActor.getSnapshot();
nestedTrace = [];
var observedOuter = capture(function () { outerActor.send("OUTER_FAULT"); });
var outerFault = outerActor.getSnapshot();
var independentCommitOk = (observedOuter === outerThrown &&
  outerFault.status === "error" && outerFault.error === outerThrown &&
  outerFault.value === outerStable.value &&
  bActor.getSnapshot().matches("Stepped") &&
  bActor.getSnapshot().context.count === 5 &&
  nestedTrace.join("|") === "actionBefore|bStep|actionAfter");
outerActor = undefined;
outerStable = undefined;
outerFault = undefined;
observedOuter = undefined;
outerThrown = undefined;
bActor.stop();
bActor = undefined;
bMachine = undefined;
aMachine = undefined;
nestedTrace = undefined;
process.memory();

var sharedThrown = { source: "inner" };
var faultBMachine = XFSM.createMachine({
  initial: "Ready",
  states: {
    Ready: { on: { FAIL: { target: "Done", actions: "fail" } } },
    Done: {}
  }
}, {
  actions: { fail: function () { throw sharedThrown; } }
});
var faultBActor = XFSM.createActor(faultBMachine).start();
var faultAActor;
var faultAMachine = XFSM.createMachine({
  initial: "Ready",
  states: {
    Ready: { on: { CALL: { target: "Done", actions: "callB" } } },
    Done: {}
  }
}, {
  actions: { callB: function () { faultBActor.send("FAIL"); } }
});
faultAActor = XFSM.createActor(faultAMachine).start();
var observedInner = capture(function () { faultAActor.send("CALL"); });
var faultASnapshot = faultAActor.getSnapshot();
var faultBSnapshot = faultBActor.getSnapshot();
var propagatedFaultOk = (observedInner === sharedThrown &&
  faultASnapshot.status === "error" && faultASnapshot.error === sharedThrown &&
  faultASnapshot.value === "Ready" && faultBSnapshot.status === "error" &&
  faultBSnapshot.error === sharedThrown && faultBSnapshot.value === "Ready");
faultAActor = undefined;
faultBActor = undefined;
faultAMachine = undefined;
faultBMachine = undefined;
faultASnapshot = undefined;
faultBSnapshot = undefined;
sharedThrown = undefined;
observedInner = undefined;
process.memory();

var gcActions = 0;
function makeGcActor() {
  var descriptor = XFSM.assign({
    count: function (context) { return context.count + 1; }
  });
  var config = {
    context: { count: 0 },
    initial: "Ready",
    states: {
      Ready: { on: { GO: { target: "Done", actions: [descriptor, "mark"] } } },
      Done: {}
    }
  };
  var options = {
    actions: { mark: function () { gcActions++; } }
  };
  return XFSM.createActor(XFSM.createMachine(config, options));
}
var gcNotices = 0;
var gcActor = makeGcActor();
var gcSubscription = gcActor.subscribe(function () { gcNotices++; });
process.memory();
if (E.defrag) E.defrag();
gcActor.start();
gcActor.send("GO");
var gcSnapshot = gcActor.getSnapshot();
var gcOk = (gcSnapshot.status === "active" && gcSnapshot.matches("Done") &&
  gcSnapshot.context.count === 1 && gcActions === 1 && gcNotices === 2);
gcSubscription.unsubscribe();
gcActor.stop();
gcSubscription = undefined;
gcActor = undefined;
gcSnapshot = undefined;
makeGcActor = undefined;
process.memory();

result = (guardOk && assignOk && actionOk && listenerOk &&
  independentCommitOk && propagatedFaultOk && gcOk);
print("TEST=xfsm_cross_actor_gc");
print((result ? "PASS " : "FAIL ") + "cross_actor_gc");
print("DONE=" + (result ? "PASS" : "FAIL"));
XFSM = undefined;
