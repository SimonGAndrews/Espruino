echo(false);
(function () {
var XFSM = require("XFSM");
var sendCount = 128;
var loopComplete = false;
var timerObservedCount = -1;
var timerObservedLoopComplete = false;
var notifications = 0;
var startTime = 0;
var elapsedMilliseconds = 0;
var passed = true;
var timer;
var actor;
var machine;
var subscription;

function report(name, ok) {
  passed = passed && ok;
  print((ok ? "PASS " : "FAIL ") + name);
}

function finish() {
  if (timer !== undefined) clearTimeout(timer);
  if (subscription) subscription.unsubscribe();
  if (actor) actor.stop();
  actor = undefined;
  machine = undefined;
  subscription = undefined;
  process.memory();
  result = passed;
  print("DONE=" + (passed ? "PASS" : "FAIL"));
}

print("TEST=xfsm_host_event_serialization");

try {
  machine = XFSM.createMachine({
    context: { count: 0 },
    initial: "Active",
    states: {
      Active: {
        on: {
          TICK: {
            actions: XFSM.assign({
              count: function (context) { return context.count + 1; }
            })
          }
        }
      }
    }
  });
  actor = XFSM.createActor(machine);
  subscription = actor.subscribe(function () { notifications++; });
  actor.start();

  timer = setTimeout(function () {
    timer = undefined;
    try {
      timerObservedCount = actor.getSnapshot().context.count;
      timerObservedLoopComplete = loopComplete;
      report("timer_waited_for_synchronous_work",
             timerObservedLoopComplete && timerObservedCount === sendCount);
      report("timer_became_due_during_work", elapsedMilliseconds >= 1);
      report("all_dispatches_published",
             notifications === sendCount + 1);
      print("METRIC synchronous_sends=" + sendCount);
      print("METRIC timer_observed_count=" + timerObservedCount);
      print("METRIC synchronous_elapsed_ms=" + elapsedMilliseconds);
    } catch (error) {
      print("FAIL timer_callback " + error);
      passed = false;
    }
    finish();
  }, 1);

  startTime = getTime();
  for (var index = 0; index < sendCount; index++) actor.send("TICK");
  elapsedMilliseconds = (getTime() - startTime) * 1000;
  loopComplete = true;
} catch (error) {
  print("FAIL setup " + error);
  passed = false;
  finish();
}
})();
