# Test-split stat comparison, phase 1b

Before: # stat record of test-split files; root = datasets/strata; columns: path<TAB>size<TAB>mtime<TAB>atime<TAB>ctime; # host=DESKTOP-KCD7BRS recorded=2026-10-18T07:04:10,727860999+09:00 mount: D: is 9p drvfs with noatime, so atime is not informative; # stat record of test-split files; root = datasets/strata; columns: path<TAB>size<TAB>mtime<TAB>atime<TAB>ctime; # host=DESKTOP-KCD7BRS recorded=2026-10-18T07:04:10,727860999+09:00 mount: D: is 9p drvfs with noatime, so atime is not informative

After: # stat record of test-split files; root = datasets/strata; columns: path<TAB>size<TAB>mtime<TAB>atime<TAB>ctime; # host=DESKTOP-KCD7BRS recorded=2026-10-18T08:47:42,813986102+09:00 mount: D: is 9p drvfs with noatime, so atime is not informative; # stat record of test-split files; root = datasets/strata; columns: path<TAB>size<TAB>mtime<TAB>atime<TAB>ctime; # host=DESKTOP-KCD7BRS recorded=2026-10-18T08:47:42,813986102+09:00 mount: D: is 9p drvfs with noatime, so atime is not informative

Files in the before record: 6644. Files in the after record: 6644.

- files that disappeared: 0
- files that appeared: 0
- size changed: 0
- mtime changed: 0
- ctime changed: 0
- atime changed: 0 (not evidence: the disk is mounted without access times; see below)


What this comparison proves: only that no file was modified. Reading a file changes neither size, mtime nor ctime, and the mount has no access times, so the comparison cannot show that a file was not opened. The evidence for that is the step logs and the committed scripts (and, from P3 on, an strace of each step).

Files the amendment allowed to be read: `spires/keble/sequences/2024-03-12-keble-college-02/`, `spires/keble/ground_truth_map/keble-college/merged-cloud-1cm.pcd`, `spires/keble/calibration/README.md`

No file outside the amendment's list was modified (size, mtime, ctime). **No modification incident.**
