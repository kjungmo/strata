#include <cmath>
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
using strata::oneLine;

namespace {
const char kNoFrame[] = "\"map\" passed to lookupTransform argument target_frame does not exist.";
const char kStale[] = "Lookup would require extrapolation into the future.  Requested time 105.2 but the "
                      "latest data is at time 100.1, when looking up transform from frame [sensor] to frame [map]";

// A node with the shipped timeouts (input 5 s, startup 30 s) on /scan in frame map.
InputVerdict judge(const InputTally& tally, double since_input, double since_integrate) {
  InputState s;
  s.tally = tally;
  s.since_input_s = since_input;
  s.since_integrate_s = since_integrate;
  return judgeInput(s, "/scan", "map");
}
double above(double x) { return std::nextafter(x, 1e9); }   // the next double after a threshold
// `ok` messages integrated, then `failed` lookups in a row, after `dropped` messages
// whose TF resolved but which were not integrated and `earlier` failures before the
// integrated ones: every count in the tally can be made different from the others.
InputTally tally(int earlier, int ok, int failed, int dropped = 0) {
  InputTally t;
  for (int i = 0; i < dropped; ++i) t.tfResolved();
  for (int i = 0; i < earlier; ++i) t.tfFailure("sensor", kNoFrame);
  for (int i = 0; i < ok; ++i) { t.tfResolved(); t.integratedOne(); }
  for (int i = 0; i < failed; ++i) t.tfFailure("sensor", kNoFrame);
  return t;
}
InputTally failures(int n) { return tally(0, 0, n); }
}  // namespace

// The tally: every message is received; a failure streak ends only when one integrates.
TEST(InputTally, CountsReceivedIntegratedAndTheFailureStreak) {
  InputTally t;
  EXPECT_EQ(t.received, 0); EXPECT_EQ(t.integrated, 0);
  EXPECT_EQ(t.tf_failures, 0); EXPECT_EQ(t.tf_failures_since_integrate, 0);
  for (int i = 0; i < 3; ++i) t.tfFailure("sensor", kNoFrame);
  EXPECT_EQ(t.received, 3); EXPECT_EQ(t.integrated, 0);
  EXPECT_EQ(t.tf_failures, 3); EXPECT_EQ(t.tf_failures_since_integrate, 3);
  t.tfResolved(); t.integratedOne();
  EXPECT_EQ(t.received, 4); EXPECT_EQ(t.integrated, 1);
  EXPECT_EQ(t.tf_failures, 3);                    // the total is kept
  EXPECT_EQ(t.tf_failures_since_integrate, 0);    // the streak is over
  t.tfFailure("sensor", kNoFrame); t.tfFailure("sensor", kNoFrame);
  EXPECT_EQ(t.received, 6); EXPECT_EQ(t.tf_failures, 5); EXPECT_EQ(t.tf_failures_since_integrate, 2);
}

// A message whose TF resolved but which is then dropped (a zero stamp with time
// windows) is received, not integrated, and does not end a failure streak.
TEST(InputTally, AResolvedButDroppedMessageDoesNotEndTheStreak) {
  InputTally t = failures(4);
  t.tfResolved();
  EXPECT_EQ(t.received, 5); EXPECT_EQ(t.integrated, 0);
  EXPECT_EQ(t.tf_failures, 4); EXPECT_EQ(t.tf_failures_since_integrate, 4);
  EXPECT_EQ(t.last_tf_error, kNoFrame);   // and the cause stays with it
  EXPECT_EQ(t.failed_frame, "sensor");
}

// last_tf_error and the failed frame are those of the latest failure, are set exactly
// while the streak runs, and are cleared when a message integrates: a status that is
// integrating must not show an error that is over.
TEST(InputTally, TheLastErrorIsSetWhileTheStreakRunsAndClearedWhenOneIntegrates) {
  InputTally t;
  EXPECT_EQ(t.last_tf_error, ""); EXPECT_EQ(t.failed_frame, "");
  t.tfFailure("laser", kNoFrame);
  EXPECT_EQ(t.last_tf_error, kNoFrame); EXPECT_EQ(t.failed_frame, "laser");
  t.tfFailure("other", std::string(kStale));
  EXPECT_EQ(t.last_tf_error, oneLine(kStale));   // the latest cause, as one bounded line
  EXPECT_LE(t.last_tf_error.size(), 120u);
  EXPECT_EQ(t.failed_frame, "other");
  t.tfResolved();                                // resolved but not integrated: the streak runs on
  EXPECT_EQ(t.failed_frame, "other"); EXPECT_FALSE(t.last_tf_error.empty());
  t.integratedOne();
  EXPECT_EQ(t.last_tf_error, ""); EXPECT_EQ(t.failed_frame, "");
  EXPECT_EQ(t.tf_failures, 2);
  // Text that sanitizes to nothing still marks the running streak.
  t.tfFailure("", "\n");
  EXPECT_EQ(t.last_tf_error, "(no error text)");
  EXPECT_EQ(t.failed_frame, "(empty frame_id)");
  t.tfFailure(std::string(200, 'f'), "line one\nline two");
  EXPECT_EQ(t.failed_frame, std::string(61, 'f') + "...");   // a frame id is bounded to 64 bytes
  EXPECT_EQ(t.last_tf_error, "line one line two");
}

// Outside text becomes one line: no control character or line break, single spaces,
// nothing at either end.
TEST(OneLine, ControlCharactersBecomeSingleSpacesAndTheEndsAreTrimmed) {
  EXPECT_EQ(oneLine(""), "");
  EXPECT_EQ(oneLine("plain text."), "plain text.");
  EXPECT_EQ(oneLine("  a\nb\r\n\tc \x01\x7f d  \n"), "a b c d");
  EXPECT_EQ(oneLine(std::string("a\0b", 3)), "a b");
  EXPECT_EQ(oneLine("a\xC2\x85" "b\xE2\x80\xA8" "c\xE2\x80\xA9" "d"), "a b c d");   // NEL, U+2028, U+2029
  EXPECT_EQ(oneLine(" \n\t "), "");
  EXPECT_EQ(oneLine(kStale).find("  "), std::string::npos);
}

// Well-formed UTF-8 passes through; every byte that is not part of a well-formed
// sequence becomes '?', so the value is always valid UTF-8.
TEST(OneLine, KeepsWellFormedUtf8AndReplacesEveryOtherByte) {
  const std::string good = "caf\xC3\xA9 \xE2\x86\x92 \xED\x95\x9C\xEA\xB8\x80 \xF0\x9F\x99\x82 \xC2\xA0";   // 2, 3, 3, 4 bytes, U+00A0
  EXPECT_EQ(oneLine(good), good);
  EXPECT_EQ(oneLine("a\xFF" "b"), "a?b");
  EXPECT_EQ(oneLine("a\x80" "b"), "a?b");                   // a lone continuation byte
  EXPECT_EQ(oneLine("a\xC3"), "a?");                        // cut short at the end
  EXPECT_EQ(oneLine("a\xE2\x86" "b"), "a??b");              // cut short in the middle
  EXPECT_EQ(oneLine("\xC0\xAF"), "??");                     // overlong 2 bytes
  EXPECT_EQ(oneLine("\xC1\xBF"), "??");
  EXPECT_EQ(oneLine("\xE0\x9F\xBF"), "???");                // overlong 3 bytes
  EXPECT_EQ(oneLine("\xE0\xA0\x80"), "\xE0\xA0\x80");       // U+0800, the first 3-byte character
  EXPECT_EQ(oneLine("\xED\xA0\x80"), "???");                // a surrogate
  EXPECT_EQ(oneLine("\xED\x9F\xBF"), "\xED\x9F\xBF");       // U+D7FF, just below them
  EXPECT_EQ(oneLine("\xF0\x8F\xBF\xBF"), "????");           // overlong 4 bytes
  EXPECT_EQ(oneLine("\xF0\x90\x80\x80"), "\xF0\x90\x80\x80");   // U+10000
  EXPECT_EQ(oneLine("\xF4\x8F\xBF\xBF"), "\xF4\x8F\xBF\xBF");   // U+10FFFF
  EXPECT_EQ(oneLine("\xF4\x90\x80\x80"), "????");           // past U+10FFFF
  EXPECT_EQ(oneLine("\xF5\x80\x80\x80"), "????");
  EXPECT_EQ(oneLine("\xF8\x88\x80\x80\x80"), "?????");      // a 5-byte form
  EXPECT_EQ(oneLine("\xC2\xA0"), "\xC2\xA0");               // U+00A0, the first after the C1 controls
  EXPECT_EQ(oneLine("a\xC2\x9F" "b"), "a b");               // U+009F, the last C1 control
  EXPECT_EQ(oneLine("\xE2\x80\xA7\xE2\x80\xAA"), "\xE2\x80\xA7\xE2\x80\xAA");   // U+2027 and U+202A stay
  EXPECT_EQ(oneLine("\xE2\x86\xA8\xE2\x86\xA9"), "\xE2\x86\xA8\xE2\x86\xA9");   // U+21A8, U+21A9: same last bytes as U+2028/9
  EXPECT_EQ(oneLine("\xC3\x89\xC3\x80"), "\xC3\x89\xC3\x80");                   // U+00C9, U+00C0: same second bytes as C1 controls
}

// At most max_bytes bytes, cut between characters, ending in "..." when cut.
TEST(OneLine, IsBoundedAndCutsBetweenCharacters) {
  EXPECT_EQ(oneLine(std::string(120, 'x')), std::string(120, 'x'));            // fits exactly: untouched
  EXPECT_EQ(oneLine(std::string(121, 'x')), std::string(117, 'x') + "...");
  EXPECT_EQ(oneLine(std::string(5000, 'x')).size(), 120u);
  EXPECT_EQ(oneLine(std::string(120, 'x') + "  \n"), std::string(120, 'x'));   // only blanks past the limit
  EXPECT_EQ(oneLine("abcdefghij", 10), "abcdefghij");
  EXPECT_EQ(oneLine("abcdefghijk", 10), "abcdefg...");
  // 2-byte characters: the cut may not split one (7 bytes left for text: 3 characters).
  const std::string e = "\xC3\xA9";
  EXPECT_EQ(oneLine(e + e + e + e + e + e, 10), e + e + e + "...");
  EXPECT_EQ(oneLine(e + e + e + e + e, 10), e + e + e + e + e);                 // exactly 10 bytes
  EXPECT_EQ(oneLine("a" + e + e + e + e + e, 10), "a" + e + e + e + "...");      // 7 bytes fit exactly
  const std::string smile = "\xF0\x9F\x99\x82";
  EXPECT_EQ(oneLine(smile + smile + smile, 10), smile + "...");
  EXPECT_EQ(oneLine("abcdef   ghijk", 10), "abcdef...");                         // no blank before the dots
  EXPECT_EQ(oneLine(std::string(300, '\xFF')), std::string(117, '?') + "...");
}

// Row 1: no message yet, inside startup_timeout_s.
TEST(InputStatus, NoInputYetInsideTheStartupGraceIsOkWaitingForInput) {
  const InputVerdict v = judge(InputTally{}, 12.0, 12.0);
  EXPECT_EQ(v.kind, InputKind::kWaitingForInput);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_EQ(v.message, "waiting for input on /scan");
  EXPECT_TRUE(v.overrides);
  EXPECT_EQ(judge(InputTally{}, 0.0, 0.0).kind, InputKind::kWaitingForInput);
  EXPECT_EQ(judge(InputTally{}, 30.0, 30.0).kind, InputKind::kWaitingForInput);   // exactly at the limit
}

// Row 2: no message after startup_timeout_s.
TEST(InputStatus, NoInputAfterTheStartupTimeoutIsError) {
  const InputVerdict v = judge(InputTally{}, 31.0, 31.0);
  EXPECT_EQ(v.kind, InputKind::kNoInputYet);
  EXPECT_EQ(v.level, kInputError);
  EXPECT_EQ(v.message, "no input on /scan yet after 31 s");
  EXPECT_TRUE(v.overrides);
  EXPECT_EQ(judge(InputTally{}, above(30.0), above(30.0)).kind, InputKind::kNoInputYet);   // just past it
}

// Row 3: messages arrived (integrated or not), then stopped for input_timeout_s. It
// outranks the TF states: without input nothing is known about TF any more.
TEST(InputStatus, InputThatStopsIsErrorWhateverTfDid) {
  for (const InputTally& t : {tally(0, 10, 0), tally(0, 10, 7), failures(7), tally(0, 1, 0), tally(0, 0, 0, 1)}) {
    const InputVerdict v = judge(t, 5.5, 40.0);
    EXPECT_EQ(v.kind, InputKind::kInputLost);
    EXPECT_EQ(v.level, kInputError);
    EXPECT_EQ(v.message, "no input on /scan for 5.5 s (sensor, driver or link down?)");
    EXPECT_TRUE(v.overrides);
    EXPECT_NE(judge(t, 5.0, 0.5).kind, InputKind::kInputLost);          // exactly at the limit: not yet
    EXPECT_EQ(judge(t, above(5.0), 0.5).kind, InputKind::kInputLost);   // just past it
  }
}

// Row 4, the case this header exists for: messages arrive, their TF lookups fail,
// nothing has integrated yet, and startup_timeout_s has not passed. The level stays OK
// (a localizer may still be starting) but the message must not say "ok".
TEST(InputStatus, FailingTfInsideTheStartupGraceIsOkButSaysNoUsableTf) {
  const InputVerdict v = judge(failures(42), 0.1, 8.0);
  EXPECT_EQ(v.kind, InputKind::kWaitingForTf);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_EQ(v.message, "no usable TF map -> sensor yet: 42 message(s) received on /scan, 0 integrated, "
                       "42 TF lookup(s) failed (WARN after startup_timeout_s = 30 s)");
  EXPECT_FALSE(v.overrides);
  // Received and failed are different counts: 20 messages resolved but were dropped, 5 failed.
  const InputVerdict mixed = judge(tally(0, 0, 5, 20), 0.1, 8.0);
  EXPECT_EQ(mixed.kind, InputKind::kWaitingForTf);
  EXPECT_EQ(mixed.message, "no usable TF map -> sensor yet: 25 message(s) received on /scan, 0 integrated, "
                           "5 TF lookup(s) failed (WARN after startup_timeout_s = 30 s)");
  // From the very first failed message, and up to exactly the limit.
  EXPECT_EQ(judge(failures(1), 0.0, 0.2).kind, InputKind::kWaitingForTf);
  EXPECT_EQ(judge(failures(1), 0.0, 0.0).kind, InputKind::kWaitingForTf);
  EXPECT_EQ(judge(failures(300), 0.1, 30.0).kind, InputKind::kWaitingForTf);
  EXPECT_EQ(judge(failures(300), 0.1, 30.0).level, kInputOk);
}

// Row 5: the same condition once startup_timeout_s has passed is WARN, as before.
TEST(InputStatus, FailingTfAfterTheStartupTimeoutIsWarn) {
  const InputVerdict v = judge(tally(0, 0, 300, 20), 0.1, 30.5);
  EXPECT_EQ(v.kind, InputKind::kTfMissing);
  EXPECT_EQ(v.level, kInputWarn);
  EXPECT_EQ(v.message, "messages arrive on /scan but the TF lookup map -> sensor has failed 300 times; "
                       "nothing integrated for 30.5 s");
  EXPECT_TRUE(v.overrides);
  EXPECT_EQ(judge(failures(300), 0.1, above(30.0)).kind, InputKind::kTfMissing);   // just past the limit
  EXPECT_EQ(judge(failures(300), 0.1, above(30.0)).level, kInputWarn);
  EXPECT_EQ(judge(failures(1), 0.1, 31.0).kind, InputKind::kTfMissing);            // one failed message is enough
}

// Row 6: integrating. Nothing to add, however long ago the last message integrated
// (a slow sensor); earlier failures (a localizer that came up late) leave no stale
// text once one message integrates.
TEST(InputStatus, IntegratingSaysNothingAndForgetsAnEarlierWait) {
  EXPECT_EQ(judge(tally(0, 50, 0), 0.02, 0.02).kind, InputKind::kFlowing);
  EXPECT_EQ(judge(tally(0, 50, 0), 3.0, 3.0).kind, InputKind::kFlowing);    // one message every 3 s
  EXPECT_EQ(judge(tally(0, 50, 0), 5.0, 5.0).kind, InputKind::kFlowing);
  EXPECT_EQ(judge(tally(0, 50, 0), 0.5, 40.0).kind, InputKind::kFlowing);   // no streak: not a TF state
  InputTally t = failures(42);
  EXPECT_EQ(judge(t, 0.1, 8.0).kind, InputKind::kWaitingForTf);
  t.tfResolved(); t.integratedOne();
  const InputVerdict v = judge(t, 0.02, 0.02);
  EXPECT_EQ(v.kind, InputKind::kFlowing);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_TRUE(v.message.empty());
  EXPECT_FALSE(v.overrides);
  EXPECT_EQ(t.tf_failures, 42);        // the total still records them
  EXPECT_EQ(t.last_tf_error, "");      // the cause does not outlive the streak
}

// Rows 7-9: TF lost in mid-run. While something integrated within the last second the
// streak shows in the values only; then the message names it at level OK; after
// input_timeout_s it is ERROR, as before. The tally has 3 earlier failures, 50
// integrated and 2 dropped messages, so every count differs from the streak.
TEST(InputStatus, TfLostMidRunIsNamedAfterOneSecondAndErrorAfterTheInputTimeout) {
  EXPECT_EQ(judge(tally(3, 50, 1, 2), 0.02, 0.4).kind, InputKind::kFlowing);
  EXPECT_EQ(judge(tally(3, 50, 9, 2), 0.02, 1.0).kind, InputKind::kFlowing);            // exactly 1 s: not yet
  EXPECT_EQ(judge(tally(3, 50, 9, 2), 0.02, above(1.0)).kind, InputKind::kTfFailing);   // just past it
  const InputVerdict note = judge(tally(3, 50, 20, 2), 0.02, 2.5);
  EXPECT_EQ(note.kind, InputKind::kTfFailing);
  EXPECT_EQ(note.level, kInputOk);
  EXPECT_EQ(note.message, "TF lookup map -> sensor failing: last 20 message(s) on /scan not integrated, "
                          "nothing integrated for 2.5 s (ERROR after input_timeout_s = 5 s)");
  EXPECT_FALSE(note.overrides);
  EXPECT_EQ(judge(tally(3, 50, 40, 2), 0.02, 5.0).kind, InputKind::kTfFailing);          // exactly at the limit
  EXPECT_EQ(judge(tally(3, 50, 40, 2), 0.02, 5.0).level, kInputOk);
  EXPECT_EQ(judge(tally(3, 50, 40, 2), 0.02, above(5.0)).kind, InputKind::kTfLost);     // just past it
  EXPECT_EQ(judge(tally(0, 1, 40), 0.02, above(5.0)).kind, InputKind::kTfLost);         // one integrated is enough
  EXPECT_EQ(judge(tally(0, 1, 40), 0.02, 29.0).level, kInputError);                     // not the startup allowance
  EXPECT_EQ(judge(tally(0, 50, 1), 2.8, 5.6).kind, InputKind::kTfLost);                 // one failed message is enough
  const InputVerdict lost = judge(tally(3, 50, 45, 2), 0.02, 5.5);
  EXPECT_EQ(lost.kind, InputKind::kTfLost);
  EXPECT_EQ(lost.level, kInputError);
  EXPECT_EQ(lost.message, "messages arrive on /scan but the TF lookup map -> sensor has failed 45 times; "
                          "nothing integrated for 5.5 s");
  EXPECT_TRUE(lost.overrides);
}

// The 1 s runs from the last integrated message, not from the first failure. A sensor
// at 2 Hz or slower therefore has one failed lookup named right away (the gap it
// leaves is two message intervals); a faster one does not.
TEST(InputStatus, ASingleFailedLookupIsNamedOnlyWhenInputIsAtMostTwoHertz) {
  // 10 Hz: the failed message came 0.1 s after the last integrated one.
  EXPECT_EQ(judge(tally(0, 50, 1), 0.0, 0.1).kind, InputKind::kFlowing);
  EXPECT_EQ(judge(tally(0, 50, 1), 0.09, 0.19).kind, InputKind::kFlowing);   // until the next one integrates
  // 1 Hz: the failed message itself comes 1 s after the last integrated one.
  EXPECT_EQ(judge(tally(0, 50, 1), 0.0, 1.0).kind, InputKind::kFlowing);
  const InputVerdict v = judge(tally(0, 50, 1), 0.5, 1.5);
  EXPECT_EQ(v.kind, InputKind::kTfFailing);
  EXPECT_EQ(v.level, kInputOk);
  EXPECT_EQ(v.message, "TF lookup map -> sensor failing: last 1 message(s) on /scan not integrated, "
                       "nothing integrated for 1.5 s (ERROR after input_timeout_s = 5 s)");
}

// The messages name the lookup with the frame id of the message that failed, and are
// true whatever the cause: no transform, a stale one (extrapolation), an unknown frame.
TEST(InputStatus, NamesTheFrameOfTheFailedMessageAndNotACause) {
  for (const char* cause : {kNoFrame, kStale, "Could not find a connection between 'map' and 'other'"}) {
    InputTally t;
    for (int i = 0; i < 6; ++i) t.tfFailure("sensor", cause);
    t.tfFailure("other", cause);
    const std::string startup = judge(t, 0.1, 8.0).message;
    EXPECT_EQ(startup, "no usable TF map -> other yet: 7 message(s) received on /scan, 0 integrated, "
                       "7 TF lookup(s) failed (WARN after startup_timeout_s = 30 s)");
    EXPECT_EQ(judge(t, 0.1, 31.0).message, "messages arrive on /scan but the TF lookup map -> other has "
                                           "failed 7 times; nothing integrated for 31.0 s");
    for (const char* claim : {"waiting", "missing", "does not exist", "extrapolation", "not published"})
      EXPECT_EQ(startup.find(claim), std::string::npos) << claim;
    EXPECT_EQ(t.last_tf_error, oneLine(cause));   // the cause is the value, not the message
  }
  InputTally t = tally(0, 5, 0);
  t.tfFailure("", kStale); t.tfFailure("", kStale);
  EXPECT_EQ(judge(t, 0.1, 2.0).message, "TF lookup map -> (empty frame_id) failing: last 2 message(s) on /scan not "
                                        "integrated, nothing integrated for 2.0 s (ERROR after input_timeout_s = 5 s)");
  t.tfFailure("bad\nframe", kStale);
  EXPECT_EQ(judge(t, 0.1, 6.0).message, "messages arrive on /scan but the TF lookup map -> bad frame has failed "
                                        "3 times; nothing integrated for 6.0 s");
}

// Messages whose TF resolves but which are all dropped (zero stamps, time windows):
// not a TF problem, so no TF text; the zero-stamp warning covers it.
TEST(InputStatus, ResolvedButDroppedMessagesAreNotATfState) {
  const InputTally t = tally(0, 0, 0, 20);
  EXPECT_EQ(judge(t, 0.05, 0.5).kind, InputKind::kFlowing);
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
  EXPECT_EQ(v.message, "no usable TF odom -> sensor yet: 5 message(s) received on /points, 0 integrated, "
                       "5 TF lookup(s) failed (WARN after startup_timeout_s = 6 s)");
  s.tally = tally(0, 3, 5);
  s.since_integrate_s = 1.5;
  EXPECT_EQ(judgeInput(s, "/points", "odom").message,
            "TF lookup odom -> sensor failing: last 5 message(s) on /points not integrated, "
            "nothing integrated for 1.5 s (ERROR after input_timeout_s = 2 s)");
  s.since_integrate_s = 2.5;
  EXPECT_EQ(judgeInput(s, "/points", "odom").kind, InputKind::kTfLost);
  s.since_input_s = 2.5;
  EXPECT_EQ(judgeInput(s, "/points", "odom").kind, InputKind::kInputLost);
  s.tally = InputTally{};
  s.since_input_s = 6.5;
  EXPECT_EQ(judgeInput(s, "/points", "odom").message, "no input on /points yet after 6 s");
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
  applyInput(judge(tally(0, 5, 0), 0.02, 0.02), level, msg);   // flowing: untouched
  EXPECT_EQ(level, kInputWarn);
  EXPECT_EQ(msg, "40% of input messages lost");
  level = kInputOk; msg = "ok";
  applyInput(judge(tally(0, 5, 0), 0.02, 0.02), level, msg);
  EXPECT_EQ(level, kInputOk);
  EXPECT_EQ(msg, "ok");

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
