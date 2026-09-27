/* Action-heavy depth-32 compiler resource harness. Requires XFC_MEASURE=1. */
echo(false);
print("TEST=xfsm_compile_pressure");
var XFSM = require("XFSM");
var leaf = {
  id: "deepLeaf",
  entry: "enter",
  exit: "exit",
  on: { RESET: { target: "#deep.L1", actions: "reset" } }
};
var depth;
for (depth = 31; depth >= 1; depth--) {
  var child = "L" + (depth + 1);
  var parent = {
    initial: child,
    entry: "enter",
    exit: "exit",
    states: {}
  };
  parent.states[child] = leaf;
  leaf = parent;
}
var config = {
  id: "deep",
  initial: "L1",
  states: { L1: leaf }
};
var options = {
  actions: {
    enter: function () {},
    exit: function () {},
    reset: function () {}
  }
};
var configured = process.memory();
XFSM._measure(true);
var constructionStarted = getTime();
var machine = XFSM.createMachine(config, options);
var constructionMilliseconds = (getTime() - constructionStarted) * 1000;
var construction = XFSM._measure(false);
var compiled = process.memory();
var report = {
  totalBlocks: configured.total,
  blockSize: configured.blocksize,
  configuredUsage: configured.usage,
  compiledUsage: compiled.usage,
  arenaBytes: machine["\xFFxfcA"].length,
  constructionPeakBlocks: construction.constructionPeakBlocks,
  constructionEndBlocks: construction.constructionEndBlocks,
  constructionMilliseconds: constructionMilliseconds,
  approximateAbsolutePeak:
      configured.usage + construction.constructionPeakBlocks
};
result = report.arenaBytes > 0 && report.constructionPeakBlocks > 0;
print("COMPILE_PRESSURE=" + JSON.stringify(report));
print("METRIC arena_bytes=" + report.arenaBytes);
print("METRIC construction_peak_blocks=" + report.constructionPeakBlocks);
print("METRIC construction_ms=" + report.constructionMilliseconds);
print("METRIC approximate_absolute_peak=" + report.approximateAbsolutePeak);
print((result ? "PASS " : "FAIL ") + "compile_pressure");
print("DONE=" + (result ? "PASS" : "FAIL"));
