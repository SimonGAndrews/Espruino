// Preload an XFSM actor before the larger radio-service role is evaluated.

(function () {
  var XFSM = require("XFSM");
  var trace = [];
  var publications = 0;
  var lastError;
  var depthLimit = 24;
  var depthActions = 0;
  var depthRuntimeOk = false;
  var leaf = {
    id : "deepLeaf",
    entry : "depthAction",
    exit : "depthAction",
    on : {DEPTH_DONE:{
      target:"#service.AwaitConfirmation",
      actions:"depthAction"
    }}
  };
  for (var depth = depthLimit - 1; depth >= 1; depth--) {
    var child = "L" + (depth + 1);
    var parent = {
      initial : child,
      entry : "depthAction",
      exit : "depthAction",
      states : {}
    };
    parent.states[child] = leaf;
    leaf = parent;
  }
  var increment = XFSM.assign(function (context) {
    return {steps:context.steps + 1};
  });
  var config = {
    id : "service",
    context : {steps:0},
    initial : "AwaitBle",
    states : {
      AwaitBle : {
        on : {BLE_CONNECTED:{target:"BleConnected", actions:[increment, "record"]}}
      },
      BleConnected : {
        on : {GATT_ACK:{target:"WifiConnecting", actions:[increment, "record"]}}
      },
      WifiConnecting : {
        on : {WIFI_CONNECTED:{target:"HttpsPending", actions:[increment, "record"]}}
      },
      HttpsPending : {
        on : {HTTPS_COMPLETE:{
          target : "#deepLeaf",
          guard : "httpsValid",
          actions : [increment, "record"]
        }}
      },
      AwaitConfirmation : {
        on : {BLE_CONFIRMED:{target:"Complete", actions:[increment, "record"]}}
      },
      Complete : {type:"final"}
    }
  };
  config.states.L1 = leaf;
  var machine = XFSM.createMachine(config, {
    actions : {
      depthAction : function () { depthActions++; },
      record : function (context, event) {
        trace.push(event.type + ":" + context.steps);
      }
    },
    guards : {
      httpsValid : function (context, event) {
        return String(event.status) === String(event.expectedStatus) &&
          event.bytes >= event.minimumBytes;
      }
    }
  });
  config = undefined;
  leaf = undefined;
  parent = undefined;
  child = undefined;
  var actor = XFSM.createActor(machine);
  var subscription = actor.subscribe(function () { publications++; });
  actor.start();
  increment = undefined;
  process.memory();

  global.XFSM_SERVICE_HOOK = {
    send : function (event) {
      var eventType = typeof event === "string" ? event : event.type;
      try {
        if (eventType === "HTTPS_COMPLETE") depthActions = 0;
        actor.send(event);
        if (eventType === "HTTPS_COMPLETE") {
          actor.send("DEPTH_DONE");
          depthRuntimeOk = depthActions === depthLimit * 2 + 1 &&
            actor.getSnapshot().matches("AwaitConfirmation");
        }
        return true;
      } catch (error) {
        lastError = "" + error;
        return false;
      }
    },
    check : function () {
      var snapshot = actor.getSnapshot();
      var expected =
        "BLE_CONNECTED:1|GATT_ACK:2|WIFI_CONNECTED:3|" +
        "HTTPS_COMPLETE:4|BLE_CONFIRMED:5";
      return {
        ok : lastError === undefined && snapshot.status === "done" &&
          snapshot.matches("Complete") && snapshot.context.steps === 5 &&
          publications === 7 && trace.join("|") === expected &&
          depthRuntimeOk,
        status : snapshot.status,
        value : snapshot.value,
        steps : snapshot.context.steps,
        publications : publications,
        trace : trace.join("|"),
        error : lastError,
        depth : depthLimit,
        depthTransition : depthRuntimeOk
      };
    },
    cleanup : function () {
      subscription.unsubscribe();
      subscription = undefined;
      actor = undefined;
      machine = undefined;
      XFSM = undefined;
      trace = undefined;
      delete global.XFSM_SERVICE_HOOK;
      process.memory();
    }
  };

  var memory = process.memory();
  print("XFSM_SERVICE_READY=" + JSON.stringify({
    state : actor.getSnapshot().value,
    jsVarsTotal : memory.total,
    jsVarsFree : memory.free,
    jsVarsUsed : memory.usage,
    jsVarBlockSize : memory.blocksize,
    depthCompiled : depthLimit
  }));
}());
