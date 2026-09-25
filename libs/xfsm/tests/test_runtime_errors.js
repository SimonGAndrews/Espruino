var XFSM = require("XFSM");
var thrown = { code: 42 };
var notices = [];
var factoryCalls = 0;
var actor;

var machine = XFSM.createMachine({
  context: function () {
    factoryCalls++;
    return { count: 0 };
  },
  initial: "Ready",
  states: {
    Ready: {
      on: {
        SAFE: {
          actions: XFSM.assign(function (context) {
            return { count: context.count + 1 };
          })
        },
        BUSY: { actions: "reenter" },
        FAIL: {
          target: "Done",
          actions: [
            XFSM.assign({ count: function () { return 99; } }),
            "fail",
            "mustNotRun"
          ]
        }
      }
    },
    Done: {}
  }
}, {
  actions: {
    reenter: function () { actor.send("SAFE"); },
    fail: function () { throw thrown; },
    mustNotRun: function () { notices.push("wrong"); }
  },
  actors: {}, services: {}, guards: {}, delays: {}
});

actor = XFSM.createActor(machine);
var before = actor.getSnapshot();
var listenerError = "listener failed";
actor.subscribe(function (snapshot) {
  notices.push("first:" + snapshot.context.count);
  if (snapshot.context.count === 1) throw listenerError;
});
actor.subscribe(function (snapshot) {
  notices.push("second:" + snapshot.context.count);
});
actor.start();
actor.start();

var observedListenerError;
try { actor.send("SAFE"); } catch (error) { observedListenerError = error; }
var afterSafe = actor.getSnapshot();
var validAfterListener = afterSafe.status === "active" &&
                         afterSafe.context.count === 1;

var busyMessage = "";
try { actor.send("BUSY"); } catch (error) { busyMessage = "" + error; }
var afterBusy = actor.getSnapshot();
var faultedByBusy = afterBusy.status === "error" &&
                    busyMessage.indexOf("E_ACTOR_BUSY") >= 0;

var faultMessage = "";
try { actor.send("SAFE"); } catch (error) { faultMessage = "" + error; }

var second = XFSM.createActor(machine);
second.start();
var stable = second.getSnapshot();
var exactThrown;
try { second.send("FAIL"); } catch (error) { exactThrown = error; }
var failed = second.getSnapshot();

var invalidEvent = false;
var third = XFSM.createActor(machine).start();
try { third.send({ payload: 1 }); }
catch (error) { invalidEvent = ("" + error).indexOf("E_EVENT_INVALID") >= 0; }

var borrowed = false;
try { third.start.call({}); }
catch (error) { borrowed = ("" + error).indexOf("E_ACTOR_INVALID") >= 0; }

result = before.status === "notStarted" &&
  factoryCalls === 3 &&
  observedListenerError === listenerError &&
  notices.join("|") === "first:0|second:0|first:1|second:1" &&
  validAfterListener && faultedByBusy &&
  afterBusy.context.count === 1 &&
  faultMessage.indexOf("E_ACTOR_FAULTED") >= 0 &&
  stable.status === "active" && stable.context.count === 0 &&
  exactThrown === thrown && failed.status === "error" &&
  failed.error === thrown && failed.value === "Ready" &&
  failed.context === stable.context &&
  notices.indexOf("wrong") < 0 && invalidEvent && borrowed;
