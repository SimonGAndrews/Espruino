// Run the ESP32-C3 radio-service workload against a preloaded XFSM actor.

(function () {
  var cfg = global.WIFI_TEST_CONFIG;
  var hook = global.XFSM_SERVICE_HOOK;
  var wifi = require("Wifi");
  var console = E.getConsole();
  var request;
  var overallTimer;
  var wifiTimer;
  var httpsTimer;
  var finished = false;
  var httpsComplete = false;
  var ack = "";
  var complete = "";
  var connected = false;
  var passes = 0;
  var failures = 0;
  var bytes = 0;
  var status = null;

  function check(name, ok, detail) {
    if (ok) passes++;
    else failures++;
    print((ok ? "PASS " : "FAIL ") + name + (detail ? " " + detail : ""));
  }

  function memory(phase) {
    var js = process.memory();
    var native = ESP32.getState();
    var value = {
      phase : phase,
      nativeFreeHeap : native.freeHeap,
      nativeMinimumHeap : native.minHeap,
      largestNativeBlock : native.largestBlock,
      jsVarsTotal : js.total,
      jsVarsFree : js.free,
      jsVarsUsed : js.usage,
      jsVarBlockSize : js.blocksize
    };
    print("INFO memory=" + JSON.stringify(value));
    return value;
  }

  function finish(reason) {
    if (finished) return;
    finished = true;
    clearTimeout(overallTimer);
    if (wifiTimer) clearTimeout(wifiTimer);
    if (httpsTimer) clearTimeout(httpsTimer);
    if (request) try { request.end(); } catch (ignore) {}
    wifi.removeAllListeners();
    wifi.disconnect();
    NRF.disconnect();
    setTimeout(function () {
      var xfsm = hook.check();
      check("xfsm_service_lifecycle", xfsm.ok,
        xfsm.ok ? "" : JSON.stringify(xfsm));
      hook.cleanup();
      hook = undefined;
      var finalMemory = memory("after_cleanup");
      print("METRIC checks_passed=" + passes);
      print("METRIC checks_failed=" + failures);
      print("BLE_HTTPS_TARGET_SUMMARY=" + JSON.stringify({
        runId : cfg.runId,
        reason : reason,
        ackReceived : ack,
        completeReceived : complete,
        httpsComplete : httpsComplete,
        observed : {httpsBytes:bytes, httpsStatus:status},
        xfsm : xfsm,
        finalMemory : finalMemory,
        checksPassed : passes,
        checksFailed : failures
      }));
      NRF.sleep();
      delete global.WIFI_TEST_CONFIG;
      print("DONE=" + (failures ? "FAIL" : "PASS"));
      E.setConsole(console, {force:false});
    }, 700);
  }

  function phaseDone(reason) {
    print("BLE_HTTPS_PHASE_DONE=" + JSON.stringify({
      runId : cfg.runId,
      reason : reason,
      success : httpsComplete,
      status : status,
      bytes : bytes,
      bleConnected : NRF.getSecurityStatus().connected
    }));
  }

  function runHTTPS() {
    check("ble_connected_before_https", NRF.getSecurityStatus().connected);
    memory("before_https");
    httpsTimer = setTimeout(function () {
      check("https_request", false, "timeout");
      phaseDone("https_timeout");
    }, cfg.httpsTimeoutMs || 25000);
    try {
      request = require("http").get(cfg.httpsURL, function (response) {
        status = response.statusCode;
        response.on("data", function (data) { bytes += data.length; });
        response.on("end", function () {
          clearTimeout(httpsTimer);
          httpsTimer = undefined;
          request = undefined;
          check("https_status",
            String(status) === String(cfg.expectedStatus), "status=" + status);
          check("https_response_body", bytes >= cfg.minimumBytes, "bytes=" + bytes);
          hook.send({
            type : "HTTPS_COMPLETE",
            status : status,
            bytes : bytes,
            expectedStatus : cfg.expectedStatus,
            minimumBytes : cfg.minimumBytes
          });
          httpsComplete = failures === 0;
          check("ble_connected_after_https", NRF.getSecurityStatus().connected);
          if (!NRF.getSecurityStatus().connected) httpsComplete = false;
          memory("after_https_response");
          phaseDone("response_complete");
        });
      });
      request.on("error", function (error) {
        clearTimeout(httpsTimer);
        httpsTimer = undefined;
        request = undefined;
        check("https_request", false, "error=" + error);
        phaseDone("https_error");
      });
    } catch (error) {
      check("https_request", false, "exception=" + error);
      phaseDone("https_exception");
    }
  }

  function connectWifi() {
    wifiTimer = setTimeout(function () {
      check("wifi_connect", false, "timeout");
      phaseDone("wifi_timeout");
    }, cfg.wifiTimeoutMs || 20000);
    wifi.connect(cfg.ssid, {password:cfg.password}, function (error) {
      clearTimeout(wifiTimer);
      wifiTimer = undefined;
      if (error) {
        check("wifi_connect", false, "error=" + error);
        phaseDone("wifi_error");
        return;
      }
      var ip = wifi.getIP();
      check("wifi_connected", !!ip.ip && ip.ip !== "0.0.0.0", "ip=" + ip.ip);
      hook.send("WIFI_CONNECTED");
      memory("after_wifi_connect");
      runHTTPS();
    });
  }

  print("TEST=xfsm_ble_wifi_https_coexistence");
  print("TARGET=" + (process.env.BOARD || "UNKNOWN"));
  E.setConsole(console, {force:true});
  wifi.removeAllListeners();
  wifi.disconnect();
  wifi.stopAP();
  NRF.wake();
  NRF.removeAllListeners();
  NRF.on("connect", function () {
    if (!connected) {
      connected = true;
      check("ble_gatt_connected", true);
      hook.send("BLE_CONNECTED");
      memory("after_ble_connect");
    }
  });
  memory("initial");

  NRF.setServices({
    0xFFF0 : {
      0xFFF1 : {value:cfg.challenge, maxLen:20, readable:true},
      0xFFF2 : {
        maxLen : 20,
        writable : true,
        onWrite : function (event) {
          ack = E.toString(event.data);
          check("ble_gatt_write_before_https", ack === cfg.ack, "value=" + ack);
          if (ack === cfg.ack && hook.send("GATT_ACK")) connectWifi();
        }
      },
      0xFFF3 : {
        maxLen : 20,
        writable : true,
        onWrite : function (event) {
          complete = E.toString(event.data);
          check("ble_gatt_write_after_https", complete === cfg.complete,
            "value=" + complete);
          if (complete === cfg.complete) hook.send("BLE_CONFIRMED");
          check("gatt_after_successful_https", httpsComplete);
          finish("post_https_gatt_received");
        }
      }
    }
  });
  NRF.setAdvertising({}, {
    name : cfg.name,
    showName : true,
    connectable : true,
    scannable : true,
    interval : 100
  });
  check("ble_gatt_advertising", NRF.getSecurityStatus().advertising);
  print("BLE_HTTPS_TARGET_READY=" + JSON.stringify({
    runId : cfg.runId,
    name : cfg.name,
    challenge : cfg.challenge
  }));
  overallTimer = setTimeout(function () {
    check("ble_https_overall", false, "timeout");
    finish("overall_timeout");
  }, cfg.overallTimeoutMs || 60000);
}());
