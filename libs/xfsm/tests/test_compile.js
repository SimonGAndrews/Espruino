var XFSM = require("XFSM");
var countExpression = function (context, event) {
  return context.count + 1;
};
var enterParent = function () {};
var ready = function () { return true; };
var record = function () {};
var increment = XFSM.assign({
  count: countExpression,
  mode: "done"
});
var config = {
  id: "machine",
  context: { count: 0 },
  initial: "Parent",
  states: {
    Parent: {
      initial: "Idle",
      entry: "enterParent",
      states: {
        Idle: {
          on: {
            GO: [{
              target: "Done",
              guard: "ready",
              actions: ["record", increment]
            }]
          }
        },
        Done: {}
      }
    },
    Outside: {}
  }
};
var machine = XFSM.createMachine(config, {
  actions: { enterParent: enterParent, record: record },
  guards: { ready: ready },
  actors: {}, services: {}, delays: {}
});
var arena = machine["\xFFxfcA"];
var retained = machine["\xFFxfcR"];

function u8(offset) {
  return arena.charCodeAt(offset);
}
function u16(offset) {
  return u8(offset) | (u8(offset + 1) << 8);
}
function u32(offset) {
  return (u16(offset) | (u16(offset + 2) << 16)) >>> 0;
}
function tableOffset(table) {
  return u32(32 + table * 8);
}
function tableCount(table) {
  return u16(36 + table * 8);
}
function stateU16(state, field) {
  return u16(tableOffset(0) + state * 32 + field);
}
function symbol(index) {
  var record = tableOffset(1) + index * 12;
  return arena.substr(u32(record + 4), u16(record + 8));
}

var originalArenaSize = arena.length;
config.initial = "Outside";
config.states.Parent.initial = "Done";

result =
  arena.substr(0, 4) === "XFCM" &&
  u16(4) === 1 &&
  u16(6) === 96 &&
  u32(8) === arena.length &&
  u16(16) === 0 &&
  u16(18) === 0 &&
  u16(20) === 6 &&
  u8(22) === 1 &&
  u32(28) === 79 &&
  tableCount(0) === 5 &&
  tableCount(1) === 11 &&
  tableCount(2) === 1 &&
  tableCount(3) === 1 &&
  tableCount(4) === 1 &&
  tableCount(5) === 3 &&
  tableCount(6) === 1 &&
  tableCount(7) === 2 &&
  stateU16(0, 2) === 1 &&
  stateU16(1, 2) === 3 &&
  stateU16(3, 8) === 0 &&
  stateU16(3, 10) === 1 &&
  symbol(0) === "Parent" &&
  symbol(1) === "done.state.machine.Parent" &&
  symbol(5) === "enterParent" &&
  symbol(6) === "GO" &&
  symbol(7) === "ready" &&
  symbol(8) === "record" &&
  symbol(9) === "count" &&
  symbol(10) === "mode" &&
  u16(tableOffset(2) + 2) === 0 &&
  u16(tableOffset(2) + 4) === 1 &&
  u16(tableOffset(3)) === 4 &&
  u16(tableOffset(3) + 2) === 0 &&
  u16(tableOffset(3) + 4) === 1 &&
  u16(tableOffset(3) + 6) === 2 &&
  u16(tableOffset(3) + 10) === 1 &&
  u16(tableOffset(4)) === 2 &&
  u16(tableOffset(5)) === 0 &&
  u16(tableOffset(5) + 2) === 1 &&
  u16(tableOffset(5) + 8) === 0 &&
  u16(tableOffset(5) + 10) === 3 &&
  u16(tableOffset(5) + 16) === 1 &&
  u16(tableOffset(5) + 18) === 0 &&
  u16(tableOffset(6)) === 1 &&
  u16(tableOffset(6) + 2) === 65535 &&
  u16(tableOffset(6) + 4) === 0 &&
  u16(tableOffset(6) + 6) === 2 &&
  u16(tableOffset(7)) === 9 &&
  u16(tableOffset(7) + 2) === 4 &&
  u16(tableOffset(7) + 4) === 1 &&
  u16(tableOffset(7) + 8) === 10 &&
  u16(tableOffset(7) + 10) === 5 &&
  u16(tableOffset(7) + 12) === 0 &&
  retained.length === 6 &&
  retained[0].count === 0 &&
  retained[1] === enterParent &&
  retained[2] === ready &&
  retained[3] === record &&
  retained[4] === countExpression &&
  retained[5] === "done" &&
  Object.keys(machine).length === 0 &&
  arena.length === originalArenaSize &&
  stateU16(0, 2) === 1 &&
  stateU16(1, 2) === 3;
