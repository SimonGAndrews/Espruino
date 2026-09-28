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

var XFCTrace = (function () {
  var caseId;
  var sequence = 0;
  var passed = true;
  var begun = false;
  var finished = false;

  function emit(kind, fields) {
    var record = {
      schema: "xfc.trace",
      version: 1,
      case: caseId,
      sequence: sequence++,
      kind: kind
    };
    var key;
    if (fields) {
      for (key in fields) {
        if (fields.hasOwnProperty(key) && fields[key] !== undefined)
          record[key] = fields[key];
      }
    }
    print(JSON.stringify(record));
  }

  function begin(id, name, classification) {
    if (begun) throw new Error("trace case already begun");
    caseId = id;
    begun = true;
    emit("case", { name: name, classification: classification });
  }

  function assertRecord(name, ok, actual, expected) {
    var fields = { name: name, pass: !!ok };
    if (arguments.length >= 3) fields.actual = actual;
    if (arguments.length >= 4) fields.expected = expected;
    if (!ok) passed = false;
    emit("assertion", fields);
    return !!ok;
  }

  function skip(name, reason) {
    emit("assertion", { name: name, status: "skip", reason: reason });
  }

  function error(operation, category, path, errorValue) {
    var errorType = typeof errorValue;
    if (errorValue instanceof TypeError) errorType = "TypeError";
    else if (errorValue instanceof Error) errorType = "Error";
    emit("error", {
      operation: operation,
      category: category,
      path: path,
      errorType: errorType
    });
  }

  function finish() {
    if (!begun) throw new Error("trace case not begun");
    if (finished) throw new Error("trace case already finished");
    finished = true;
    emit("result", { pass: passed });
    result = passed;
    return passed;
  }

  return {
    begin: begin,
    call: function (operation, input) {
      emit("call", { operation: operation, input: input });
    },
    action: function (name, context, eventType) {
      emit("action", { name: name, context: context, eventType: eventType });
    },
    context: function (point, value) {
      emit("context", { point: point, value: value });
    },
    snapshot: function (point, snapshot, context) {
      emit("snapshot", {
        point: point,
        status: snapshot.status,
        value: snapshot.value,
        context: context
      });
    },
    error: error,
    assert: assertRecord,
    skip: skip,
    finish: finish
  };
})();
