echo(false);
(function () {
var XFSM = require("XFSM");
var Storage = require("Storage");
var moduleName = "xfc_m7";
var trace = [];
var passed = true;
var timer;
var actor;
var machine;
var subscription;
var config;
var options;
var flashModule;
var outerAction;
var closureAction;
var nativeHigh;
var nativeLow;
var stableSnapshot;
var failedSnapshot;
var observedFault;
var initialUsage = process.memory().usage;
var pinHigh = -1;
var pinLow = -1;
var closureCalls = 0;
var fault = { source: "xfc_m7_flash_module" };

function report(name, ok) {
  passed = passed && ok;
  print((ok ? "PASS " : "FAIL ") + name);
}

function removeCachedModule() {
  if (Modules.getCached().indexOf(moduleName) >= 0)
    Modules.removeCached(moduleName);
}

function finish() {
  if (timer !== undefined) clearTimeout(timer);
  if (subscription) subscription.unsubscribe();
  digitalWrite(LED1, 0);

  actor = undefined;
  machine = undefined;
  subscription = undefined;
  config = undefined;
  options = undefined;
  flashModule = undefined;
  outerAction = undefined;
  closureAction = undefined;
  nativeHigh = undefined;
  nativeLow = undefined;
  stableSnapshot = undefined;
  failedSnapshot = undefined;
  observedFault = undefined;
  process.memory();

  removeCachedModule();
  Storage.erase(moduleName);
  report("storage_cleanup", Storage.read(moduleName) === undefined);
  report("module_cache_cleanup",
         Modules.getCached().indexOf(moduleName) < 0);
  report("safe_pin_state", digitalRead(LED1) === 0);

  var finalUsage = process.memory().usage;
  print("METRIC initial_usage_blocks=" + initialUsage);
  print("METRIC final_usage_blocks=" + finalUsage);
  print("METRIC trace=" + trace.join("|"));
  delete global.__xfcM7Trace;
  delete global.__xfcM7Fault;
  result = passed;
  print("DONE=" + (passed ? "PASS" : "FAIL"));
}

print("TEST=xfsm_host_application");

try {
  pinMode(LED1, "output");
  digitalWrite(LED1, 0);
  removeCachedModule();
  Storage.erase(moduleName);
  Storage.write(moduleName,
    "exports.guard=function(c,e){return c.enabled===true&&e.allow===true;};" +
    "exports.action=function(c,e){global.__xfcM7Trace.push('flash:'+e.value);};" +
    "exports.fail=function(){global.__xfcM7Trace.push('flashFail');" +
      "throw global.__xfcM7Fault;};");

  global.__xfcM7Trace = trace;
  global.__xfcM7Fault = fault;
  flashModule = require(moduleName);

  outerAction = function (context, event) {
    trace.push("outer:" + event.value);
  };
  closureAction = (function (prefix) {
    return function (context, event) {
      closureCalls++;
      trace.push(prefix + ":" + event.value);
    };
  })("closure");
  nativeHigh = digitalWrite.bind(undefined, LED1, 1);
  nativeLow = digitalWrite.bind(undefined, LED1, 0);

  config = {
    context: { enabled: true },
    initial: "Idle",
    states: {
      Idle: {
        on: {
          CHECK: {
            guard: "nativeReady",
            actions: "recordNativeGuard"
          },
          RUN: {
            target: "Active",
            guard: "flashReady",
            actions: ["outerAction", "closureAction", "flashAction"]
          }
        }
      },
      Active: {
        entry: ["nativeHigh", "observeHigh"],
        exit: ["nativeLow", "observeLow"],
        on: {
          TIMER: { target: "Done", actions: "flashAction" }
        }
      },
      Done: {
        on: {
          FAIL: { target: "Idle", actions: "flashFail" }
        }
      }
    }
  };
  options = {
    actions: {
      recordNativeGuard: function () { trace.push("nativeGuard"); },
      outerAction: outerAction,
      closureAction: closureAction,
      flashAction: flashModule.action,
      flashFail: flashModule.fail,
      nativeHigh: nativeHigh,
      nativeLow: nativeLow,
      observeHigh: function () {
        pinHigh = digitalRead(LED1);
        trace.push("pinHigh:" + pinHigh);
      },
      observeLow: function () {
        pinLow = digitalRead(LED1);
        trace.push("pinLow:" + pinLow);
      }
    },
    guards: {
      nativeReady: Boolean,
      flashReady: flashModule.guard
    }
  };

  machine = XFSM.createMachine(config, options);
  actor = XFSM.createActor(machine);
  subscription = actor.subscribe(function (snapshot) {
    trace.push("sub:" + snapshot.value);
  });

  removeCachedModule();
  config = undefined;
  options = undefined;
  flashModule = undefined;
  outerAction = undefined;
  closureAction = undefined;
  nativeHigh = undefined;
  nativeLow = undefined;
  process.memory();

  actor.start();
  actor.send("CHECK");
  actor.send({ type: "RUN", allow: true, value: 3 });

  report("retained_outer_and_closure_actions",
         closureCalls === 1 && trace.indexOf("outer:3") >= 0 &&
         trace.indexOf("closure:3") >= 0);
  report("native_guard", trace.indexOf("nativeGuard") >= 0);
  report("flash_guard_and_action", trace.indexOf("flash:3") >= 0);
  report("bound_native_action", pinHigh === 1 && digitalRead(LED1) === 1);
  report("module_source_in_storage",
         Storage.read(moduleName) !== undefined &&
         Modules.getCached().indexOf(moduleName) < 0);

  timer = setTimeout(function () {
    timer = undefined;
    try {
      trace.push("timerBefore");
      actor.send({ type: "TIMER", value: 7 });
      trace.push("timerAfter");
      stableSnapshot = actor.getSnapshot();

      report("timer_event_ingress",
             stableSnapshot.matches("Done") && pinLow === 0 &&
             trace.indexOf("flash:7") >= 0);
      report("synchronous_timer_dispatch",
             trace.indexOf("timerBefore") < trace.indexOf("flash:7") &&
             trace.indexOf("flash:7") < trace.indexOf("timerAfter"));
      report("subscription_publication",
             trace.indexOf("sub:Active") >= 0 &&
             trace.indexOf("sub:Done") >= 0);

      try { actor.send("FAIL"); }
      catch (error) { observedFault = error; }
      failedSnapshot = actor.getSnapshot();
      report("flash_callback_fault",
             observedFault === fault && failedSnapshot.status === "error" &&
             failedSnapshot.error === fault &&
             failedSnapshot.matches("Done") &&
             failedSnapshot.context === stableSnapshot.context);
      report("callback_order",
             trace.join("|") ===
             "sub:Idle|nativeGuard|sub:Idle|outer:3|closure:3|flash:3|" +
             "pinHigh:1|sub:Active|timerBefore|pinLow:0|flash:7|" +
             "sub:Done|timerAfter|flashFail");
    } catch (error) {
      print("FAIL timer_callback " + error);
      passed = false;
    }
    finish();
  }, 25);
} catch (error) {
  print("FAIL setup " + error);
  passed = false;
  finish();
}
})();
