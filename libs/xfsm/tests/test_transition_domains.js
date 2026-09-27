echo(false);
(function () {
var XFSM = require("XFSM");
var trace = [];

function mark(name) {
  return function () { trace.push(name); };
}

var machine = XFSM.createMachine({
  id: "domains",
  initial: "Parent",
  entry: "enterRoot",
  exit: "exitRoot",
  states: {
    Parent: {
      entry: "enterParent",
      exit: "exitParent",
      initial: { target: "A", actions: "initParent" },
      states: {
        A: {
          entry: "enterA",
          exit: "exitA",
          on: {
            TARGETLESS: { reenter: true, actions: "targetless" },
            BLOCK: {},
            ATOMIC_SELF: { target: "A", actions: "atomicSelf" },
            ATOMIC_REENTER: {
              target: "A", reenter: true, actions: "atomicReenter"
            }
          }
        },
        B: {
          entry: "enterB",
          exit: "exitB",
          initial: { target: "Deep", actions: "initB" },
          states: {
            Deep: {
              entry: "enterDeep",
              exit: "exitDeep",
              on: {
                TO_ANCESTOR: {
                  target: "#domains.Parent", actions: "toAncestor"
                },
                TO_ANCESTOR_REENTER: {
                  target: "#domains.Parent", reenter: true,
                  actions: "toAncestorReenter"
                },
                TO_SIBLING: {
                  target: "Sibling", actions: "toSibling"
                },
                CROSS: {
                  target: "#domains.Other.C.Leaf", actions: "cross"
                },
                CROSS_REENTER: {
                  target: "#domains.Other.C.Leaf", reenter: true,
                  actions: "crossReenter"
                }
              }
            },
            Sibling: { entry: "enterSibling", exit: "exitSibling" }
          }
        }
      },
      on: {
        COMPOUND_SELF: {
          target: "Parent", actions: "compoundSelf"
        },
        COMPOUND_REENTER: {
          target: "Parent", reenter: true, actions: "compoundReenter"
        },
        TO_DESCENDANT: {
          target: ".B.Deep", actions: "toDescendant"
        },
        TO_DESCENDANT_REENTER: {
          target: ".B.Deep", reenter: true,
          actions: "toDescendantReenter"
        }
      }
    },
    Other: {
      entry: "enterOther",
      exit: "exitOther",
      initial: "C",
      states: {
        C: {
          entry: "enterC",
          exit: "exitC",
          initial: "Leaf",
          states: {
            Leaf: { entry: "enterLeaf", exit: "exitLeaf" }
          }
        }
      }
    }
  },
  on: {
    BLOCK: { actions: "wrong" },
    ROOT_TO_DESCENDANT: {
      target: ".Parent.B.Deep", actions: "rootToDescendant"
    },
    ROOT_REENTER_DESCENDANT: {
      target: ".Parent.B.Deep", reenter: true,
      actions: "rootReenterDescendant"
    }
  }
}, {
  actions: {
    enterRoot: mark("enterRoot"), exitRoot: mark("exitRoot"),
    enterParent: mark("enterParent"), exitParent: mark("exitParent"),
    initParent: mark("initParent"),
    enterA: mark("enterA"), exitA: mark("exitA"),
    enterB: mark("enterB"), exitB: mark("exitB"), initB: mark("initB"),
    enterDeep: mark("enterDeep"), exitDeep: mark("exitDeep"),
    enterSibling: mark("enterSibling"), exitSibling: mark("exitSibling"),
    enterOther: mark("enterOther"), exitOther: mark("exitOther"),
    enterC: mark("enterC"), exitC: mark("exitC"),
    enterLeaf: mark("enterLeaf"), exitLeaf: mark("exitLeaf"),
    targetless: mark("targetless"), wrong: mark("wrong"),
    atomicSelf: mark("atomicSelf"), atomicReenter: mark("atomicReenter"),
    compoundSelf: mark("compoundSelf"),
    compoundReenter: mark("compoundReenter"),
    toDescendant: mark("toDescendant"),
    toDescendantReenter: mark("toDescendantReenter"),
    rootToDescendant: mark("rootToDescendant"),
    rootReenterDescendant: mark("rootReenterDescendant"),
    toAncestor: mark("toAncestor"),
    toAncestorReenter: mark("toAncestorReenter"),
    toSibling: mark("toSibling"),
    cross: mark("cross"), crossReenter: mark("crossReenter")
  }
});

function run(event, setup) {
  var actor = XFSM.createActor(machine).start();
  trace = [];
  if (setup) {
    actor.send(setup);
    trace = [];
  }
  actor.send(event);
  var observed = trace.join("|");
  var snapshot = actor.getSnapshot();
  trace = [];
  actor.stop();
  actor = undefined;
  process.memory();
  return { trace: observed, value: snapshot.value };
}

function check(event, setup, expectedTrace, expectedValue) {
  var observed = run(event, setup);
  var value = JSON.stringify(observed.value);
  var expected = JSON.stringify(expectedValue);
  if (observed.trace !== expectedTrace || value !== expected)
    print("MISMATCH=" + event + "|" + observed.trace + "|" + value);
  return observed.trace === expectedTrace && value === expected;
}

var nestedTrace = [];
var nestedMachine = XFSM.createMachine({
  id: "nestedInitial",
  initial: "Parent",
  states: {
    Parent: {
      initial: { target: "Nested", actions: "initParentNested" },
      states: {
        Nested: {
          initial: { target: "Leaf", actions: "initNested" },
          states: { Leaf: {} }
        }
      },
      on: {
        NESTED_COMPOUND_SELF: {
          target: "Parent", actions: "nestedCompoundSelf"
        }
      }
    }
  }
}, {
  actions: {
    initParentNested: function () { nestedTrace.push("initParentNested"); },
    initNested: function () { nestedTrace.push("initNested"); },
    nestedCompoundSelf: function () {
      nestedTrace.push("nestedCompoundSelf");
    }
  }
});

function checkNestedCompoundSelf() {
  var actor = XFSM.createActor(nestedMachine).start();
  nestedTrace = [];
  actor.send("NESTED_COMPOUND_SELF");
  var observed = nestedTrace.join("|");
  var value = JSON.stringify(actor.getSnapshot().value);
  actor.stop();
  actor = undefined;
  process.memory();
  return observed === "nestedCompoundSelf|initNested" &&
         value === JSON.stringify({ Parent: { Nested: "Leaf" } });
}

var ok =
  check("TARGETLESS", undefined, "targetless", { Parent: "A" }) &&
  check("BLOCK", undefined, "", { Parent: "A" }) &&
  check("ATOMIC_SELF", undefined, "atomicSelf", { Parent: "A" }) &&
  check("ATOMIC_REENTER", undefined,
        "exitA|atomicReenter|enterA", { Parent: "A" }) &&
  check("COMPOUND_SELF", undefined,
        "exitA|compoundSelf|enterA", { Parent: "A" }) &&
  check("COMPOUND_REENTER", undefined,
        "exitA|exitParent|compoundReenter|enterParent|initParent|enterA",
        { Parent: "A" }) &&
  check("TO_DESCENDANT", undefined,
        "exitA|toDescendant|enterB|enterDeep",
        { Parent: { B: "Deep" } }) &&
  check("TO_DESCENDANT_REENTER", undefined,
        "exitA|exitParent|toDescendantReenter|enterParent|enterB|enterDeep",
        { Parent: { B: "Deep" } }) &&
  check("ROOT_TO_DESCENDANT", undefined,
        "exitA|exitParent|rootToDescendant|enterParent|enterB|enterDeep",
        { Parent: { B: "Deep" } }) &&
  check("ROOT_REENTER_DESCENDANT", undefined,
        "exitA|exitParent|exitRoot|rootReenterDescendant|enterRoot|" +
        "enterParent|enterB|enterDeep", { Parent: { B: "Deep" } }) &&
  check("TO_ANCESTOR", "TO_DESCENDANT",
        "exitDeep|exitB|exitParent|toAncestor|enterParent|initParent|enterA",
        { Parent: "A" }) &&
  check("TO_ANCESTOR_REENTER", "TO_DESCENDANT",
        "exitDeep|exitB|exitParent|toAncestorReenter|enterParent|" +
        "initParent|enterA", { Parent: "A" }) &&
  check("TO_SIBLING", "TO_DESCENDANT",
        "exitDeep|toSibling|enterSibling", { Parent: { B: "Sibling" } }) &&
  check("CROSS", "TO_DESCENDANT",
        "exitDeep|exitB|exitParent|cross|enterOther|enterC|enterLeaf",
        { Other: { C: "Leaf" } }) &&
  check("CROSS_REENTER", "TO_DESCENDANT",
        "exitDeep|exitB|exitParent|crossReenter|enterOther|enterC|enterLeaf",
        { Other: { C: "Leaf" } }) &&
  checkNestedCompoundSelf();

result = ok;
print("TEST=xfsm_transition_domains");
print((result ? "PASS " : "FAIL ") + "transition_domains");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
