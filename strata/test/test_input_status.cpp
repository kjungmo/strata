#include <string>
#include <gtest/gtest.h>
#include "strata/input_status.hpp"
using strata::InputKind;
using strata::InputState;
using strata::InputTally;
using strata::InputVerdict;
using strata::applyInput;
using strata::judgeInput;
using strata::kInputError;
using strata::kInputOk;
using strata::kInputWarn;

namespace {
// A node with the shipped timeouts (input 5 s, startup 30 s) on /scan in frame map.
InputVerdict judge(const InputTally& tally, double since_input, double since_integrate) {
  InputState s;
  s.tally = tally;
  s.since_input_s = since_input;
  s.since_integrate_s = since_integrate;
  return judgeInput(s, "/scan", "map");
}
InputTally failures(int n) { InputTally t; for (int i = 0; i < n; ++i) t.tfFailure(); return t; }
InputTally integratedThenFailures(int ok, int failed) {
  InputTally t;
  for (int i = 0; i < ok; ++i) { t.tfResolved(); t.integratedOne(); }
  for (int i = 0; i < failed; ++i) t.tfFailure();
  return t;
}
}  // namespace

// The tally: every message is received; a failure streak ends only when one integrates.
TEST(InputTally, CountsReceivedIntegratedAndTheFailureStreak) {
  InputTally t;
  EXPECT_EQ(t.received, 0); EXPECT_EQ(t.integrated, 0);
  EXPECT_EQ(t.tf_failures, 0); EXPECT_EQ(t.tf_failures_since_integrate, 0);
  for (int i = 0; i < 3; ++i) t.tfFailure();
  EXPECT_EQ(t.received, 3); EXPECT_EQ(t.integrated, 0);
  EXPECT_EQ(t.tf_failures, 3); EXPECT_EQ(t.tf_failures_since_integrate, 3);
  t.tfResolved(); t.integratedOne();
  EXPECT_EQ(t.received, 4); EXPECT_EQ(t.integrated, 1);
  EXPECT_EQ(t.tf_failures, 3);                    // the total is kept
  EXPECT_EQ(t.tf_failures_since_integrate, 0);    // the streak is over
  t.tfFailure(); t.tfFailure();
  EXPECT_EQ(t.received, 6); EXPECT_EQ(t.tf_failures, 5); EXPECT_EQ(t.tf_failures_since_integrate, 2);
}

// A message whose TF resolved but which is then dropped (a zero stamp with time
// windows) is received, not integrated, and does not end a failure streak.
TEST(InputTally, AResolvedButDroppedMessageDoesNotEndTheStreak) {
  InputTally t = failures(4);
  t.tfResolved();
  EXPECT_EQ(t.received, 5); EXPECT_EQ(t.integrated, 0);
  EXPECT_EQ(t.tf_failures, 4); EXPECT_EQ(t.tf_failures_since_integrate, 4);
}

// Row 1: no message yet, inside startup_timeout_s.
TEST(InputStatus, NoInputYetInsideTheStartupGraceIsOkWaitingForInput) {
  const InputVerdict v = judge(InputTally{}, 12.0, 12.0);
  EXPECT_EQ(v.kind, InputKind::kWaitingForInput);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_EQ(v.message, "waiting for input on /scan");
  EXPECT_TRUE(v.overrides);
  EXPECT_EQ(judge(InputTally{}, 30.0, 30.0).kind, InputKind::kWaitingForInput);   // at the limit
}

// Row 2: no message after startup_timeout_s.
TEST(InputStatus, NoInputAfterTheStartupTimeoutIsError) {
  const InputVerdict v = judge(InputTally{}, 31.0, 31.0);
  EXPECT_EQ(v.kind, InputKind::kNoInputYet);
  EXPECT_EQ(v.level, kInputError);
  EXPECT_EQ(v.message, "no input on /scan yet after 31 s");
  EXPECT_TRUE(v.overrides);
}

// Row 3: messages arrived (integrated or not), then stopped for input_timeout_s. It
// outranks the TF states: without input nothing is known about TF any more.
TEST(InputStatus, InputThatStopsIsErrorWhateverTfDid) {
  for (const InputTally& t : {integratedThenFailures(10, 0), integratedThenFailures(10, 7), failures(7)}) {
    const InputVerdict v = judge(t, 5.5, 40.0);
    EXPECT_EQ(v.kind, InputKind::kInputLost);
    EXPECT_EQ(v.level, kInputError);
    EXPECT_EQ(v.message, "no input on /scan for 5.5 s (sensor, driver or link down?)");
    EXPECT_TRUE(v.overrides);
  }
}

// Row 4, the case this header exists for: scans arrive, every TF lookup fails, nothing
// has integrated yet, and startup_timeout_s has not passed. The level stays OK (a
// localizer may still be starting) but the message must not say "ok".
TEST(InputStatus, FailingTfInsideTheStartupGraceIsOkButSaysWaitingForTf) {
  const InputVerdict v = judge(failures(42), 0.1, 8.0);
  EXPECT_EQ(v.kind, InputKind::kWaitingForTf);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_EQ(v.message, "waiting for TF map -> sensor: 42 message(s) received on /scan, 0 integrated, "
                       "42 TF lookup(s) failed (WARN after startup_timeout_s = 30 s)");
  EXPECT_FALSE(v.overrides);
  // From the very first failed message, and up to the limit.
  EXPECT_EQ(judge(failures(1), 0.0, 0.2).kind, InputKind::kWaitingForTf);
  EXPECT_EQ(judge(failures(300), 0.1, 30.0).kind, InputKind::kWaitingForTf);
}

// Row 5: the same condition once startup_timeout_s has passed is WARN, as before.
TEST(InputStatus, FailingTfAfterTheStartupTimeoutIsWarn) {
  const InputVerdict v = judge(failures(300), 0.1, 30.5);
  EXPECT_EQ(v.kind, InputKind::kTfMissing);
  EXPECT_EQ(v.level, kInputWarn);
  EXPECT_EQ(v.message, "messages arrive on /scan but the TF lookup map -> sensor has failed 300 times; "
                       "nothing integrated for 30.5 s");
  EXPECT_TRUE(v.overrides);
}

// Row 6: integrating. Nothing to add; earlier failures (a localizer that came up
// late) leave no stale "waiting" text once one message integrates.
TEST(InputStatus, IntegratingSaysNothingAndForgetsAnEarlierWait) {
  EXPECT_EQ(judge(integratedThenFailures(50, 0), 0.02, 0.02).kind, InputKind::kFlowing);
  InputTally t = failures(42);
  EXPECT_EQ(judge(t, 0.1, 8.0).kind, InputKind::kWaitingForTf);
  t.tfResolved(); t.integratedOne();
  const InputVerdict v = judge(t, 0.02, 0.02);
  EXPECT_EQ(v.kind, InputKind::kFlowing);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_TRUE(v.message.empty());
  EXPECT_EQ(t.tf_failures, 42);   // the total still records them
  // Even far past startup_timeout_s (the node has been up for minutes).
  EXPECT_EQ(judge(t, 0.02, 0.02).kind, InputKind::kFlowing);
}

// Row 7: TF lost in mid-run. Up to kTfNoteAfterS the streak shows in the values only
// (one late transform must not flicker the message); then the message names it at
// level OK; after input_timeout_s it is ERROR, as before.
TEST(InputStatus, TfLostMidRunIsNamedAfterOneSecondAndErrorAfterTheInputTimeout) {
  EXPECT_EQ(judge(integratedThenFailures(50, 1), 0.02, 0.4).kind, InputKind::kFlowing);
  EXPECT_EQ(judge(integratedThenFailures(50, 9), 0.02, 1.0).kind, InputKind::kFlowing);
  const InputVerdict note = judge(integratedThenFailures(50, 20), 0.02, 2.5);
  EXPECT_EQ(note.kind, InputKind::kTfFailing);
  EXPECT_EQ(note.level, kInputOk);
  EXPECT_EQ(note.message, "TF lookup map -> sensor failing: last 20 message(s) on /scan not integrated, "
                          "nothing integrated for 2.5 s (ERROR after input_timeout_s = 5 s)");
  EXPECT_FALSE(note.overrides);
  EXPECT_EQ(judge(integratedThenFailures(50, 40), 0.02, 5.0).kind, InputKind::kTfFailing);   // at the limit
  const InputVerdict lost = judge(integratedThenFailures(50, 45), 0.02, 5.5);
  EXPECT_EQ(lost.kind, InputKind::kTfLost);
  EXPECT_EQ(lost.level, kInputError);
  EXPECT_EQ(lost.message, "messages arrive on /scan but the TF lookup map -> sensor has failed 45 times; "
                          "nothing integrated for 5.5 s");
  EXPECT_TRUE(lost.overrides);
}

// Messages whose TF resolves but which are all dropped (zero stamps, time windows):
// not a TF problem, so no TF text; the zero-stamp warning covers it.
TEST(InputStatus, ResolvedButDroppedMessagesAreNotATfState) {
  InputTally t;
  for (int i = 0; i < 20; ++i) t.tfResolved();
  EXPECT_EQ(judge(t, 0.05, 12.0).kind, InputKind::kFlowing);
  EXPECT_EQ(judge(t, 0.05, 45.0).kind, InputKind::kFlowing);
}

// The timeouts are the node's parameters, not constants.
TEST(InputStatus, UsesTheConfiguredTimeouts) {
  InputState s;
  s.tally = failures(5);
  s.since_input_s = 0.1; s.since_integrate_s = 8.0;
  s.input_timeout_s = 2.0; s.startup_timeout_s = 6.0;
  EXPECT_EQ(judgeInput(s, "/points", "odom").kind, InputKind::kTfMissing);
  s.since_integrate_s = 5.0;
  const InputVerdict v = judgeInput(s, "/points", "odom");
  EXPECT_EQ(v.kind, InputKind::kWaitingForTf);
  EXPECT_EQ(v.message, "waiting for TF odom -> sensor: 5 message(s) received on /points, 0 integrated, "
                       "5 TF lookup(s) failed (WARN after startup_timeout_s = 6 s)");
  s.tally = integratedThenFailures(3, 5);
  s.since_integrate_s = 2.5;
  EXPECT_EQ(judgeInput(s, "/points", "odom").kind, InputKind::kTfLost);
  s.since_input_s = 2.5;
  EXPECT_EQ(judgeInput(s, "/points", "odom").kind, InputKind::kInputLost);
}

// Folding the verdict into what the window and stamp checks found.
TEST(InputStatus, ANoteKeepsTheLevelAndAnOverrideReplacesIt) {
  const InputVerdict wait = judge(failures(3), 0.1, 2.0);
  int level = kInputOk; std::string msg = "ok";
  applyInput(wait, level, msg);                      // a bare "ok" is replaced, never kept
  EXPECT_EQ(level, kInputOk);
  EXPECT_EQ(msg, wait.message);

  level = kInputWarn; msg = "3 message(s) in the last second had a zero header stamp";
  applyInput(wait, level, msg);                      // after another warning it is appended
  EXPECT_EQ(level, kInputWarn);
  EXPECT_EQ(msg, "3 message(s) in the last second had a zero header stamp; " + wait.message);

  level = kInputWarn; msg = "40% of input messages lost";
  applyInput(judge(integratedThenFailures(5, 0), 0.02, 0.02), level, msg);   // flowing: untouched
  EXPECT_EQ(level, kInputWarn);
  EXPECT_EQ(msg, "40% of input messages lost");

  const InputVerdict missing = judge(failures(300), 0.1, 31.0);
  level = kInputOk; msg = "ok";
  applyInput(missing, level, msg);
  EXPECT_EQ(level, kInputWarn);
  EXPECT_EQ(msg, missing.message);

  const InputVerdict waiting = judge(InputTally{}, 3.0, 3.0);   // "waiting for input" resets to OK
  level = kInputWarn; msg = "stale";
  applyInput(waiting, level, msg);
  EXPECT_EQ(level, kInputOk);
  EXPECT_EQ(msg, "waiting for input on /scan");
}
