echo(false);
var XFSM = require("XFSM");
var Storage = require("Storage");
var phaseFile = "xfc_save_phase";
var exitFile = "xfc_reset_exit";

Storage.erase(phaseFile);
Storage.erase(exitFile);

function capture(fn) {
  try { fn(); }
  catch (error) { return error; }
  return undefined;
}

function has(error, category) {
  return ("" + error).indexOf(category) >= 0;
}

var xfcTrace = [];
var xfcSavedSnapshot;
var xfcMachine = XFSM.createMachine({
  context: { count: 0 },
  initial: "Ready",
  states: {
    Ready: {
      entry: "enterReady",
      exit: "exitReady",
      on: {
        SAVE: {
          target: "Restored",
          guard: "saveGuard",
          actions: [
            XFSM.assign({
              count: function (context) {
                xfcTrace.push("assignSave");
                save();
                return context.count + 1;
              }
            }),
            "requestSave",
            "afterSaveRequest"
          ]
        }
      }
    },
    Restored: {
      entry: "enterRestored",
      exit: "exitRestored",
      on: {
        AFTER: { target: "Complete", actions: "afterRestore" }
      }
    },
    Complete: { type: "final", entry: "enterComplete" }
  }
}, {
  guards: {
    saveGuard: function () {
      xfcTrace.push("guardSave");
      save();
      return true;
    }
  },
  actions: {
    enterReady: function () { xfcTrace.push("enterReady"); },
    exitReady: function () { xfcTrace.push("exitReady"); },
    requestSave: function () { xfcTrace.push("requestSave"); save(); },
    afterSaveRequest: function () { xfcTrace.push("afterSaveRequest"); },
    enterRestored: function () { xfcTrace.push("enterRestored"); },
    exitRestored: function () { xfcTrace.push("exitRestored"); },
    afterRestore: function () { xfcTrace.push("afterRestore"); },
    enterComplete: function () { xfcTrace.push("enterComplete"); }
  }
});

var xfcActor = XFSM.createActor(xfcMachine);
var xfcNotices = 0;
var xfcSubscription = xfcActor.subscribe(function (snapshot) {
  xfcNotices++;
  xfcTrace.push("notice:" + snapshot.status + ":" + snapshot.value);
  if (snapshot.matches("Restored")) {
    xfcSavedSnapshot = snapshot;
    save();
  }
});

var xfcLifecycleMachine = XFSM.createMachine({
  initial: "Ready",
  states: {
    Ready: { on: { FINISH: "Complete" } },
    Complete: { type: "final" }
  }
});
var xfcNotStartedActor = XFSM.createActor(xfcLifecycleMachine);
var xfcNotStartedSnapshot = xfcNotStartedActor.getSnapshot();
var xfcDoneActor = XFSM.createActor(xfcLifecycleMachine).start();
xfcDoneActor.send("FINISH");
var xfcDoneSnapshot = xfcDoneActor.getSnapshot();
var xfcStoppedActor = XFSM.createActor(xfcLifecycleMachine).start().stop();
var xfcStoppedSnapshot = xfcStoppedActor.getSnapshot();

var xfcFaultValue = { source: "savedActor" };
var xfcFaultMachine = XFSM.createMachine({
  initial: "Ready",
  states: { Ready: { on: { FAIL: { actions: "fail" } } } }
}, {
  actions: { fail: function () { throw xfcFaultValue; } }
});
var xfcFaultActor = XFSM.createActor(xfcFaultMachine).start();
capture(function () { xfcFaultActor.send("FAIL"); });
var xfcFaultSnapshot = xfcFaultActor.getSnapshot();

var xfcDiscardMachine = XFSM.createMachine({
  initial: "Active",
  states: { Active: { exit: "recordDiscardExit" } }
}, {
  actions: {
    recordDiscardExit: function () { Storage.write(exitFile, "exit"); }
  }
});
var xfcDiscardActor = XFSM.createActor(xfcDiscardMachine).start();

E.on("init", function () {
  if (Storage.read(phaseFile) === undefined) {
    Storage.write(phaseFile, "saved");
    setTimeout(function () { ESP32.reboot(); }, 500);
    return;
  }

  var passed = true;
  var expectedBefore = [
    "enterReady",
    "notice:active:Ready",
    "guardSave",
    "exitReady",
    "assignSave",
    "requestSave",
    "afterSaveRequest",
    "enterRestored",
    "notice:active:Restored"
  ].join("|");

  try {
    var restored = xfcActor.getSnapshot();
    passed = passed && restored === xfcSavedSnapshot;
    passed = passed && restored.status === "active";
    passed = passed && restored.matches("Restored");
    passed = passed && restored.context.count === 1;
    passed = passed && xfcNotices === 2;
    passed = passed && xfcTrace.join("|") === expectedBefore;
    passed = passed && xfcDiscardActor.getSnapshot().matches("Active");
    passed = passed && Storage.read(exitFile) === undefined;

    passed = passed && xfcNotStartedActor.getSnapshot() ===
      xfcNotStartedSnapshot;
    passed = passed && xfcNotStartedSnapshot.status === "notStarted";
    passed = passed && xfcDoneActor.getSnapshot() === xfcDoneSnapshot;
    passed = passed && xfcDoneSnapshot.status === "done";
    passed = passed && xfcStoppedActor.getSnapshot() === xfcStoppedSnapshot;
    passed = passed && xfcStoppedSnapshot.status === "stopped";
    passed = passed && xfcFaultActor.getSnapshot() === xfcFaultSnapshot;
    passed = passed && xfcFaultSnapshot.status === "error";
    passed = passed && xfcFaultSnapshot.error === xfcFaultValue;

    passed = passed && xfcNotStartedActor.start() === xfcNotStartedActor;
    passed = passed && xfcNotStartedActor.getSnapshot().status === "active";
    xfcNotStartedActor.stop();
    passed = passed && has(capture(function () { xfcDoneActor.start(); }),
                           "E_ACTOR_STATE");
    passed = passed && xfcDoneActor.send("IGNORED") === undefined;
    passed = passed && has(capture(function () { xfcStoppedActor.start(); }),
                           "E_ACTOR_STATE");
    passed = passed && xfcStoppedActor.send("IGNORED") === undefined;
    passed = passed && has(capture(function () { xfcFaultActor.start(); }),
                           "E_ACTOR_FAULTED");
    passed = passed && has(capture(function () {
      xfcFaultActor.send("IGNORED");
    }), "E_ACTOR_FAULTED");
    passed = passed && has(capture(function () { xfcFaultActor.stop(); }),
                           "E_ACTOR_FAULTED");

    var beforeStart = xfcTrace.length;
    passed = passed && xfcActor.start() === xfcActor;
    passed = passed && xfcTrace.length === beforeStart;

    xfcActor.send("AFTER");
    var complete = xfcActor.getSnapshot();
    passed = passed && complete.status === "done";
    passed = passed && complete.matches("Complete");
    passed = passed && xfcNotices === 3;
    passed = passed && xfcTrace.join("|") === expectedBefore +
      "|exitRestored|afterRestore|enterComplete|notice:done:Complete";
    xfcSubscription.unsubscribe();

    var freshActor = XFSM.createActor(xfcMachine).start();
    passed = passed && freshActor.getSnapshot().matches("Ready");
    freshActor.stop();
  } catch (error) {
    passed = false;
    print("INFO save_restore_error=" + error);
  }

  print("TEST=xfsm_save_restore");
  print((passed ? "PASS " : "FAIL ") + "save_restore");
  print("DONE=" + (passed ? "PASS" : "FAIL"));
  setTimeout(function () { reset(true); }, 750);
});

xfcActor.start();
xfcActor.send("SAVE");
