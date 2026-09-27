echo(false);
(function () {
var XFSM = require("XFSM");

var omittedMachine = XFSM.createMachine({
  initial: "Idle",
  states: { Idle: {} }
});
var omittedA = XFSM.createActor(omittedMachine);
var omittedB = XFSM.createActor(omittedMachine);
var omittedStopped = XFSM.createActor(omittedMachine);
var omittedBefore = omittedA.getSnapshot();
omittedStopped.stop();
var omittedStoppedSnapshot = omittedStopped.getSnapshot();
omittedA.start();
omittedB.start();
var omittedContextA = omittedA.getSnapshot().context;
var omittedContextB = omittedB.getSnapshot().context;
var omittedOk = omittedBefore.context === undefined &&
  omittedStoppedSnapshot.status === "stopped" &&
  omittedStoppedSnapshot.context === undefined &&
  typeof omittedContextA === "object" &&
  typeof omittedContextB === "object" &&
  omittedContextA !== omittedContextB &&
  Object.keys(omittedContextA).length === 0 &&
  Object.keys(omittedContextB).length === 0;

var nested = { mode: "shared" };
var list = [1, 2];
var literal = { count: 0, nested: nested, list: list };
var literalMachine = XFSM.createMachine({
  context: literal,
  initial: "Idle",
  states: {
    Idle: {
      on: {
        CHANGE: {
          actions: XFSM.assign({
            count: function (context) { return context.count + 1; },
            added: "actor-a"
          })
        }
      }
    }
  }
});
var literalA = XFSM.createActor(literalMachine).start();
var literalB = XFSM.createActor(literalMachine).start();
var literalInitialA = literalA.getSnapshot().context;
var literalInitialB = literalB.getSnapshot().context;
literalA.send("CHANGE");
var literalChangedA = literalA.getSnapshot().context;
var literalCurrentB = literalB.getSnapshot().context;
var literalOk = literalInitialA === literal &&
  literalInitialB === literal &&
  literalChangedA !== literal &&
  literalChangedA.count === 1 &&
  literalChangedA.added === "actor-a" &&
  literalChangedA.nested === nested &&
  literalChangedA.list === list &&
  literal.count === 0 && literal.added === undefined &&
  literalCurrentB === literal && literalCurrentB.count === 0;

var factoryCalls = 0;
var entryContexts = [];
var factoryMachine = XFSM.createMachine({
  context: function () {
    factoryCalls++;
    return { actorNumber: factoryCalls, nested: { owner: factoryCalls } };
  },
  initial: "Idle",
  states: { Idle: { entry: "observeEntry" } }
}, {
  actions: {
    observeEntry: function (context, event) {
      entryContexts.push(context);
      entryContexts.push(event.type);
    }
  }
});
var factoryA = XFSM.createActor(factoryMachine);
var factoryB = XFSM.createActor(factoryMachine);
var factoryStopped = XFSM.createActor(factoryMachine);
var factoryDeferred = factoryCalls === 0 &&
  factoryA.getSnapshot().context === undefined;
factoryStopped.stop();
var factoryStillDeferred = factoryCalls === 0;
factoryA.start();
factoryA.start();
factoryB.start();
var factoryContextA = factoryA.getSnapshot().context;
var factoryContextB = factoryB.getSnapshot().context;
var factoryOk = factoryDeferred && factoryStillDeferred &&
  factoryCalls === 2 &&
  factoryContextA !== factoryContextB &&
  factoryContextA.nested !== factoryContextB.nested &&
  factoryContextA.actorNumber === 1 &&
  factoryContextB.actorNumber === 2 &&
  entryContexts.length === 4 &&
  entryContexts[0] === factoryContextA &&
  entryContexts[1] === "xstate.init" &&
  entryContexts[2] === factoryContextB &&
  entryContexts[3] === "xstate.init";

result = omittedOk && literalOk && factoryOk;
if (!result)
  print("CHECKS=" + omittedOk + "," + literalOk + "," + factoryOk);
print("TEST=xfsm_context_ownership");
print((result ? "PASS " : "FAIL ") + "context_ownership");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
