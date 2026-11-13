# Design note: a multi-day 3D benchmark (not implemented)

Status: proposal. Nothing in this repository runs it. Facts about datasets were read on the pages
named next to them; anything not found there is written "not verified".

## Purpose

The claim "remove what changed, keep what persists" has only been exercised in 2D and on synthetic
data. Single-pass recordings cannot test it: a voxel is touched in few windows (SPEC 3.2a, "Input density
(3D)"), and nothing changes between visits. A benchmark needs several sessions of one place in 3D LiDAR,
with poses good enough to align them.

## Candidate datasets

### NCLT (first candidate)
- Sensor: Velodyne HDL-32E (also Ladybug3 camera, IMU, fibre-optic gyro, RTK GPS). http://robots.engin.umich.edu/nclt/
- Sessions and span: 27 sessions, 8 Jan 2012 to 5 Apr 2013, routes and times of day vary; 34.9 h, 147.4 km in
  total (about 1.29 h and 5.5 km per session). Same page.
- Size on disk: Velodyne archive 16 GB for session 2012-01-08; the same page lists 95 GB for 2012-03-31, which
  looks like a typo. Total Velodyne size: not verified.
- Licence: ODbL 1.0, contents under DbCL 1.0; cite the IJRR 2016 paper; adapted public versions stay open. Same page.
- Pose source: ground truth from a SLAM graph, interpolated by odometry to about 100 Hz; a covariance file per
  session. Same page. Its accuracy as a reference for map-change evaluation: not verified.
- Download: per-session `.tar.gz` on Amazon S3 through the `downloader.py` script (`--date`, `--vel`, `--continue`
  to resume). Same page.
- Caveat: routes vary between sessions, so session pairs must be chosen by overlap (see protocol).

### Boreas (second candidate)
- Sensor: 128-beam Velodyne Alpha-Prime (also Navtech radar, camera). https://arxiv.org/abs/2203.10168
- Span: one year, repeated route, over 350 km of driving. Same page. Number of sequences: not verified.
- Pose source: post-processed Applanix POS LV GNSS/INS poses per frame, in a global ENU frame; no ground truth for
  test-set sequences. https://github.com/utiasASRL/pyboreas
- Download: AWS CLI, `aws s3 sync s3://boreas <dir>` (`--no-sign-request` without an account); per-sequence
  commands from the project site. Same repository page.
- Licence: code BSD-3-Clause; dataset licence (DATA_LICENSE.md): not verified. Total size: not verified.

Not listed because not verified here: other repeated-route 3D datasets.

## Protocol sketch

1. Session selection. Pick one place and pairs or triples of sessions with at least 50 % overlap of
   their trajectories (to fix, before looking at results). Split sessions into a calibration group
   and a test group. No session is in both.
2. Pose source. Use the dataset's reference poses for all sessions. Do not run a localizer to build the map.
   Report the pose uncertainty the dataset gives, since a map-change score cannot beat it.
3. Map building. Feed sessions in time order through the node (or the library) with the full cloud, no thinning.
   Use the window length and voxel size of the shipped 3D file unless the calibration group says otherwise.
4. Metrics, each on the test group only:
   - Change detection: voxels the later session shows empty (or occupied) against the earlier map; precision and
     recall of "removed" and "kept" against a reference built from the later session alone.
   - Static-map completeness and off-reference share: coverage of reference structure and the share of static
     voxels farther than a stated distance from it, as in SPEC 3.2a.
   - Downstream localization on a held-out session: tracking error and share of accepted registrations against the map.
5. Baselines: plain accumulation of every hit voxel, and the same run with `evidence_model: legacy`.
6. Options to compare, each as one arm: the default, a lower (m0, m1) pair, `export_undecided`, `periodic_min_periods` 2.

## Compute and storage estimate (to confirm)

Velodyne data are about 16 GB per NCLT session (one measured example). Three sessions are then about 50 GB
before extraction. Map size follows the cells touched (README, Deployment cautions, Memory), so check the Periodic veto
setting named there and watch peak memory. Run time: not estimated.

## To pre-register before any test session is read

- The session split, the pair list and the overlap rule.
- The pair (m0, m1) and window length, chosen on the calibration group only.
- The distance for "off-reference", the pass levels for each metric, and the arms above.
- What counts as a failure (for example any arm where localization error exceeds the plain-accumulation baseline by a stated margin).
- That single runs are reported with a spread over at least the sessions available, not as one number.

## Operational note

Put large data on a separate data disk, not on the system disk. Check free space before the first download,
and use the resume option of the downloader after an interruption.
