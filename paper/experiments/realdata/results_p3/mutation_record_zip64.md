# Mutation record: zip64 reader (PREREG_P3.md Amendment 10)

Code: `ZipArchive` in `p1b_io.hpp`. Tests: `Zip.ReadsZip64SizesAndOffsets` (entry fields in the extra field; foreign extra field first; end record with only the directory offset flagged), `Zip.ReadsZip64DeflatedMembersWithDifferentSizes`,
`Zip.ReadsMembersBeyondFourGiB` (sparse file, first member at 4.5e9 bytes), `Zip.RejectsBrokenZip64` (missing extra field, missing locator, wrong zip64 record signature, wrong locator signature).
Runner: copy of `p1b_io.hpp`, one replacement per mutant, build, `test_p1b --gtest_filter=Zip*`. All 40 tests of `test_p1b` pass on the unmutated code (the 35 earlier tests are unchanged: non-zip64 archives take the old path).

First pass (before the tests of mode 4 to 6 existed): 12 mutants, 8 killed, 4 survivors (zip64 record signature unchecked, locator signature unchecked, trigger on the entry count only, entry count from the disk-count field).
Tests were added for the first three. Second pass: 11 killed, 1 survivor.

| # | Mutant | Result |
|---|---|---|
| 1 | usize and csize taken in the other order | killed |
| 2 | offset not taken from the extra field | killed |
| 3 | locator 19 bytes before the end record | killed |
| 4 | extra field id 2 instead of 1 | killed |
| 5 | foreign extra field not skipped | killed |
| 6 | directory offset from the 32-bit field of the zip64 record | killed |
| 7 | entry count from the "entries on this disk" field | survives: equivalent (a single-disk archive has equal values; multi-disk zip64 is rejected by the reader) |
| 8 | zip64 record signature not checked | killed (second pass) |
| 9 | locator signature not checked | killed (second pass) |
| 10 | zip64 record offset taken from the wrong locator field | killed |
| 11 | zip64 trigger on the entry count alone | killed (second pass) |
| 12 | missing extra field accepted | killed |
