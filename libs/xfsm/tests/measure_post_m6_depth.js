/* Compact depth-32 resource harness. Requires XFC_MEASURE=1. */
echo(false);
print("TEST=xfsm_post_m6_depth");
var XFSM = require("XFSM");
var initial = process.memory();
var node = {};
var index;
var child;
for (index = 31; index >= 1; index--) {
  child = {};
  child["S" + (index + 1)] = node;
  node = { initial: "S" + (index + 1), states: child };
}
node.on = { DEPTH: {} };
var states = {};
states.S1 = node;
var config = { initial: "S1", states: states };
var configured = process.memory();
var before = XFSM._memoryUsage();
XFSM._measure(true);
var machine = XFSM.createMachine(config);
var construction = XFSM._measure(false);
var after = XFSM._memoryUsage();

config = undefined;
states = undefined;
node = undefined;
child = undefined;
process.memory();
var actor = XFSM.createActor(machine).start();
var snapshot = actor.getSnapshot();
XFSM._measure(true);
actor.send("DEPTH");
var send = XFSM._measure(false);

var report = {
  initialUsage: initial.usage,
  configuredUsage: configured.usage,
  totalBlocks: configured.total,
  blockSize: configured.blocksize,
  arenaBytes: machine["\xFFxfcA"].length,
  machinePersistentBlocks: after - before,
  constructionPeakBlocks: construction.constructionPeakBlocks,
  constructionEndBlocks: construction.constructionEndBlocks,
  snapshotValue: snapshot.value,
  sendStackBytes: send.lastStackBytes,
  maximumStackBytes: send.maximumStackBytes
};
result = report.arenaBytes > 0 && report.constructionPeakBlocks > 0 && snapshot.status === "active";
print("POST_M6_DEPTH=" + JSON.stringify(report));
print("METRIC arena_bytes=" + report.arenaBytes);
print("METRIC construction_peak_blocks=" + report.constructionPeakBlocks);
print("METRIC maximum_stack_bytes=" + report.maximumStackBytes);
print((result ? "PASS " : "FAIL ") + "post_m6_depth");
print("DONE=" + (result ? "PASS" : "FAIL"));
actor.stop();
