# Mutation record: repeated read of a cache frame (PREREG_P3.md Amendment 11)

Code: `CacheReader::next` in `p1b_io.hpp`. Tests: `Cache.ShortFileFailsAfterTheRetriesWithTheOldMessage` (a really short file: header cut and points cut; 6 retries; old message with the frame index),
`Cache.ATransientShortReadIsRepeatedAndReturnsTheRightFrame` (a helper thread completes the cut file while the reader waits; the frame content is checked). 42 tests of `test_p1b` pass on the unmutated code.
Runner: one replacement per mutant, build, `test_p1b --gtest_filter=Cache*`.

| # | Mutant | Result |
|---|---|---|
| 1 | no retries (`kReadRetries = 0`) | killed (the transient test aborts on the exception) |
| 2 | five retries | killed (after the test compared to the literal 6; the first version compared to the constant and the mutant survived) |
| 3 | stream state not cleared before the repeat | killed |
| 4 | no seek back to the start of the frame | killed |
| 5 | retry counter not incremented | killed |
| 6 | gives up one attempt late | killed |
| 7 | header message replaced by the points message | killed |
| 8 | frame index off by one in the message | killed |
| 9 | start position taken four bytes late | killed |
