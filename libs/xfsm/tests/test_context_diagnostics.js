echo(false);
(function () {
var XFSM = require("XFSM");

function constructionFails(config, options, category, path) {
  try {
    XFSM.createMachine(config, options);
  } catch (error) {
    var message = "" + error;
    var matches = message.indexOf(category) >= 0 &&
                  message.indexOf(path) >= 0;
    if (!matches) print("MISMATCH=" + category + "|" + path + "|" + message);
    return matches;
  }
  print("NO_ERROR=" + category + "|" + path);
  return false;
}

function assignmentFails(value) {
  try {
    XFSM.assign(value);
  } catch (error) {
    var message = "" + error;
    return message.indexOf("E_CONFIG_TYPE") >= 0 &&
           message.indexOf("assign.assignment") >= 0;
  }
  return false;
}

var basic = function (context) { return context; };
var invalidConfigOk =
  constructionFails({ context: null, initial: "A", states: { A: {} } },
                    undefined, "E_CONFIG_TYPE", "config.context") &&
  constructionFails({ context: [], initial: "A", states: { A: {} } },
                    undefined, "E_CONFIG_TYPE", "config.context") &&
  constructionFails({ context: 1, initial: "A", states: { A: {} } },
                    undefined, "E_CONFIG_TYPE", "config.context") &&
  assignmentFails(null) && assignmentFails([]) && assignmentFails("bad");

var accessorMap = {};
Object.defineProperty(accessorMap, "value", {
  get: function () { return 1; }
});
var accessorOk = assignmentFails(accessorMap);

var namedDescriptor = XFSM.assign({ value: 1 });
var namedDescriptorOk = constructionFails({
  initial: "A",
  states: { A: { entry: "bad" } }
}, {
  actions: { bad: namedDescriptor }
}, "E_CONFIG_TYPE", "options.actions.bad");

var invalidValues = [null, [], 1];
var invalidFactoryOk = true;
for (var i = 0; i < invalidValues.length; i++) {
  (function (invalid) {
    var entered = 0;
    var machine = XFSM.createMachine({
      context: function () { return invalid; },
      initial: "A",
      states: { A: { entry: "entered" } }
    }, {
      actions: { entered: function () { entered++; } }
    });
    var actor = XFSM.createActor(machine);
    var message = "";
    try { actor.start(); } catch (error) { message = "" + error; }
    var snapshot = actor.getSnapshot();
    invalidFactoryOk = invalidFactoryOk &&
      message.indexOf("E_CONTEXT_INVALID") >= 0 &&
      message.indexOf("actor.start.context") >= 0 &&
      snapshot.status === "error" && snapshot.value === undefined &&
      snapshot.context === undefined && entered === 0;
  })(invalidValues[i]);
}

var factoryThrown = { source: "factory" };
var factoryEntered = 0;
var throwingFactoryMachine = XFSM.createMachine({
  context: function () { throw factoryThrown; },
  initial: "A",
  states: { A: { entry: "entered" } }
}, {
  actions: { entered: function () { factoryEntered++; } }
});
var throwingFactoryActor = XFSM.createActor(throwingFactoryMachine);
var observedFactoryThrown;
try { throwingFactoryActor.start(); }
catch (error) { observedFactoryThrown = error; }
var factoryFailure = throwingFactoryActor.getSnapshot();
var factoryThrowOk = observedFactoryThrown === factoryThrown &&
  factoryFailure.status === "error" &&
  factoryFailure.error === factoryThrown &&
  factoryFailure.value === undefined &&
  factoryFailure.context === undefined && factoryEntered === 0;

var invalidPartialOk = true;
for (var j = 0; j < invalidValues.length; j++) {
  (function (invalid) {
    var invalidPartialMachine = XFSM.createMachine({
      context: { count: 0 },
      initial: "A",
      states: {
        A: {
          on: {
            BAD: {
              actions: XFSM.assign(function () { return invalid; })
            }
          }
        }
      }
    });
    var invalidPartialActor = XFSM.createActor(invalidPartialMachine).start();
    var invalidPartialStable = invalidPartialActor.getSnapshot();
    var invalidPartialMessage = "";
    try { invalidPartialActor.send("BAD"); }
    catch (error) { invalidPartialMessage = "" + error; }
    var invalidPartialFailed = invalidPartialActor.getSnapshot();
    invalidPartialOk = invalidPartialOk &&
      invalidPartialMessage.indexOf("E_CONTEXT_INVALID") >= 0 &&
      invalidPartialMessage.indexOf("actor.runtime.assign") >= 0 &&
      invalidPartialFailed.status === "error" &&
      invalidPartialFailed.value === "A" &&
      invalidPartialFailed.context === invalidPartialStable.context &&
      invalidPartialFailed.context.count === 0;
  })(invalidValues[j]);
}

function assignmentFailure(assignment, thrown) {
  var machine = XFSM.createMachine({
    context: { count: 0 },
    initial: "A",
    states: {
      A: {
        on: {
          FAIL: {
            target: "B",
            actions: [
              XFSM.assign({ count: function () { return 1; } }),
              assignment
            ]
          }
        }
      },
      B: {}
    }
  });
  var actor = XFSM.createActor(machine).start();
  var stable = actor.getSnapshot();
  var observed;
  try { actor.send("FAIL"); } catch (error) { observed = error; }
  var failed = actor.getSnapshot();
  return observed === thrown && failed.status === "error" &&
    failed.error === thrown && failed.value === "A" &&
    failed.context === stable.context && failed.context.count === 0;
}

var partialThrown = { source: "partial" };
var partialThrowOk = assignmentFailure(XFSM.assign(function () {
  throw partialThrown;
}), partialThrown);

var getterThrown = { source: "partial-property" };
var partialWithGetter = function () {
  var partial = {};
  Object.defineProperty(partial, "value", {
    get: function () { throw getterThrown; }
  });
  return partial;
};
var propertyReadOk = assignmentFailure(
  XFSM.assign(partialWithGetter), getterThrown);

var expressionThrown = { source: "expression" };
var expressionMachine = XFSM.createMachine({
  context: { count: 0 },
  initial: "A",
  states: {
    A: {
      on: {
        FAIL: {
          target: "B",
          actions: [
            XFSM.assign({ count: function () { return 1; } }),
            XFSM.assign({ value: function () { throw expressionThrown; } })
          ]
        }
      }
    },
    B: {}
  }
});
var expressionActor = XFSM.createActor(expressionMachine).start();
var expressionStable = expressionActor.getSnapshot();
var observedExpressionThrown;
try { expressionActor.send("FAIL"); }
catch (error) { observedExpressionThrown = error; }
var expressionFailed = expressionActor.getSnapshot();
var expressionOk = observedExpressionThrown === expressionThrown &&
  expressionFailed.status === "error" &&
  expressionFailed.error === expressionThrown &&
  expressionFailed.value === "A" &&
  expressionFailed.context === expressionStable.context &&
  expressionFailed.context.count === 0;

result = invalidConfigOk && accessorOk && namedDescriptorOk &&
  invalidFactoryOk && factoryThrowOk && invalidPartialOk &&
  partialThrowOk && propertyReadOk && expressionOk;
if (!result)
  print("CHECKS=" + [invalidConfigOk, accessorOk, namedDescriptorOk,
        invalidFactoryOk, factoryThrowOk, invalidPartialOk, partialThrowOk,
        propertyReadOk, expressionOk].join(","));
print("TEST=xfsm_context_diagnostics");
print((result ? "PASS " : "FAIL ") + "context_diagnostics");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
