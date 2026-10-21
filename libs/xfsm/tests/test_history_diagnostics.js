/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

echo(false);
(function () {
var XFSM = require("XFSM");

function fails(config, category, path) {
  try {
    XFSM.createMachine(config);
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

function accepts(config) {
  try {
    XFSM.createMachine(config);
    return true;
  } catch (error) {
    print("UNEXPECTED=" + error);
    return false;
  }
}

function owner(history, extra) {
  var config = {
    initial: "away",
    states: {
      away: {},
      owner: {
        initial: "a",
        states: { history: history, a: {}, b: {} }
      }
    }
  };
  if (extra) extra(config);
  return config;
}

var checks = [];
checks.push(accepts(owner({ type: "history", history: "deep" })));
checks.push(fails(owner({ type: "history", history: "other" }),
  "E_CONFIG_TYPE", "config.states.owner.states.history.history"));
checks.push(fails(owner({ type: "history", history: true }),
  "E_CONFIG_TYPE", "config.states.owner.states.history.history"));
checks.push(fails({ type: "history" }, "E_CONFIG_TYPE", "config.type"));
checks.push(fails({
  initial: "history",
  states: { history: { type: "history" }, ordinary: {} }
}, "E_CONFIG_TYPE", "config.states.history.type"));
checks.push(fails({
  initial: "owner",
  states: {
    owner: {
      initial: "a",
      states: {
        first: { type: "history" },
        second: { type: "history" },
        a: {}
      }
    }
  }
}, "E_UNSUPPORTED_FEATURE", "config.states.owner.states.second.type"));
checks.push(fails(owner({ type: "history", entry: "wrong" }),
  "E_UNKNOWN_PROPERTY", "config.states.owner.states.history.entry"));
checks.push(fails({
  initial: "owner",
  states: {
    owner: {
      initial: "history",
      states: { history: { type: "history" }, a: {} }
    }
  }
}, "E_INITIAL_UNKNOWN", "config.states.owner.initial"));
checks.push(fails(owner({ type: "history", target: "missing" }),
  "E_TARGET_UNKNOWN", "config.states.owner.states.history.target"));
checks.push(fails(owner({ type: "history", target: "owner" }),
  "E_TARGET_UNKNOWN", "config.states.owner.states.history.target"));
checks.push(fails(owner({ type: "history", target: "#deep" }, function (config) {
  config.states.owner.states.a = {
    initial: "deep", states: { deep: { id: "deep" } }
  };
}), "E_TARGET_UNKNOWN", "config.states.owner.states.history.target"));
checks.push(accepts(owner({
  type: "history", history: "deep", target: "#deep"
}, function (config) {
  config.states.owner.states.a = {
    initial: "deep", states: { deep: { id: "deep" } }
  };
})));
checks.push(fails(owner({
  type: "history", history: "deep", target: "#outside"
}, function (config) {
  config.states.away.id = "outside";
}), "E_TARGET_UNKNOWN", "config.states.owner.states.history.target"));
checks.push(fails(owner({ type: "history" }, function (config) {
  config.states.owner.states.a.on = {
    AGAIN: { target: "#insideOwner.history" }
  };
  config.states.owner.id = "insideOwner";
}), "E_UNSUPPORTED_FEATURE",
  "config.states.owner.states.a.on.AGAIN.target"));

result = checks.indexOf(false) < 0;
if (!result) print("CHECKS=" + checks.join(","));
print("TEST=xfsm_history_diagnostics");
print((result ? "PASS " : "FAIL ") + "history_diagnostics");
print("DONE=" + (result ? "PASS" : "FAIL"));
})();
