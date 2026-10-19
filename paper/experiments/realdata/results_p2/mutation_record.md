# Mutation record, phase 2 (strata_core 0.2.0)

Made by `mutation_p2.py` on a scratch copy; the repository was not modified. Sweeps: `engine` (code mutants), `defaults`
(mutants of the files that state the shipped defaults), `tests` (every numeric literal in an EXPECT_/ASSERT_ line of
`test_sprt.cpp` changed once; survivors are classified by `survivor_class`, none is unexplained).
Engine survivors are all equivalent mutants with the reason in the row, except the legacy one, which the bit-equivalence script kills.
A mutant that hangs counts as killed (timeout).
Run at strata_core tree d8db00e (after the review fixes: the count restart at demotion, the Unknown read-out of a rejected periodic
candidate pinned, the floor pinned at n = T). Two engine mutants are new since the first record: "rejected periodic candidate reads
Transient" and "no count restart at demotion". "floor from n > T" survived the previous test file once the count restart changed the
path; the door test now kills it.

## engine: 75 mutants, 66 killed, 9 survived, 0 build errors

| mutant | where | result | killed by (first) |
|---|---|---|---|
| l_hit inverted | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.NoisyWallRarelyEverDemotedByPeriodicPathWithSpending, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence |
| l_miss inverted | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.NoisyWallRarelyEverDemotedByPeriodicPathWithSpending, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence |
| A without (1 - beta) | src/layered_map.cpp | killed | ReplayP1.DerivedTableOfTheAdvice, Sprt.DerivedQuantitiesOfThePairsInTheAdvice, Sprt.DerivedQuantitiesOfTheShippedPair |
| B with alpha and beta swapped | src/layered_map.cpp | killed | Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| B without (1 - alpha) | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.DerivedTableOfTheAdvice, ReplayP1.TruncationTableAgainstTheAdvisorsMonteCarloAtM |
| ceiling one hit lower | src/layered_map.cpp | killed | Sprt.DerivedQuantitiesOfTheShippedPair, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| ceiling three hits | src/layered_map.cpp | killed | Sprt.DerivedQuantitiesOfTheShippedPair, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| demotion level lower | src/layered_map.cpp | killed | Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| demotion level higher | src/layered_map.cpp | killed | Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| m* sign | src/layered_map.cpp | killed | ReplayP1.DerivedTableOfTheAdvice, ReplayP1.NMaxOfTheShippedPairIsTheSmallestMultipleOf16UnderThreePercent, ReplayP1.TruncationTableAgainstTheAdvisorsMonteCarloAtM |
| latency loop <= | src/layered_map.cpp | killed | Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling, Sprt.ValidationRejectsEachBadInputOnBothSides |
| demote_misses loop >= | src/layered_map.cpp | survived; EQUIVALENT: equivalent: a step lands exactly on the level only for a rational ratio of the increments |  |
| window cap 1e7 | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| window cap 1e5 | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| no cap on the demotion loop | src/layered_map.cpp | survived; EQUIVALENT: equivalent: the loop guard behind it throws the same invalid_argument after 1e6 steps |  |
| m0 > 0 | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| m0 < m1 | src/layered_map.cpp | survived; EQUIVALENT: equivalent: m0 == m1 gives l_hit = 0, and the window cap rejects it |  |
| m1 < 1 | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| alpha > 0 | src/layered_map.cpp | survived; EQUIVALENT: equivalent: alpha = 0 gives A = infinity, and the window cap rejects it |  |
| beta > 0 | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| alpha + beta < 1 | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| n_max >= 0 | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.DerivedTableOfTheAdvice, ReplayP1.InSampleKittiReplayOfTheAdvice |
| ceiling finite | src/layered_map.cpp | killed | Sprt.ValidationRejectsEachBadInputOnBothSides |
| ceiling >= A | src/layered_map.cpp | killed | Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling, Sprt.ValidationRejectsEachBadInputOnBothSides |
| ceiling NaN derives | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.NoisyWallRarelyEverDemotedByPeriodicPathWithSpending, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence |
| model switch | src/layered_map.cpp | killed | Grid2DBackend.CloseWindowsClosesRegardlessOfTickCount, Grid2DBackend.FreeSurvivesPruning, Grid2DBackend.RenderOccupancyGrid |
| sprt path never taken | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.InSampleKittiReplayOfTheAdvice |
| increments swapped | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence, LayeredMap.RevisitLoopWall8of48StaysStatic |
| no ceiling | src/layered_map.cpp | killed | Sprt.CeilingCapsTheStatisticOfAnUngraduatedPeriodicCandidate, Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| ceiling clamp >= | src/layered_map.cpp | survived; EQUIVALENT: equivalent: assigns the value it already has |  |
| floor before the gate (the rejected design) | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| floor from n > T | src/layered_map.cpp | killed | Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| floor direction | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence, LayeredMap.RevisitLoopWall8of48StaysStatic |
| no floor at all | src/layered_map.cpp | killed | Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic, Sprt.PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone |
| graduation ignores the Periodic veto | src/layered_map.cpp | killed | Sprt.PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone |
| graduate at l > A | src/layered_map.cpp | survived; EQUIVALENT: equivalent: l = A exactly does not occur for irrational increments |  |
| graduate at A + 0.5 | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence, ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences |
| no restart at the ceiling | src/layered_map.cpp | killed | Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| restart at A, not the ceiling | src/layered_map.cpp | killed | Sprt.AlwaysHitWallGraduatesAtTheDerivedWindowAndNeverEarlier, Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| periodic cell not demoted | src/layered_map.cpp | killed | Sprt.DoorGraduatesEarlyAndPeriodicityThenDemotesIt, Sprt.PeriodicWinsOverIntermittent |
| demote at l < level | src/layered_map.cpp | survived; EQUIVALENT: equivalent: l = level exactly does not occur for irrational increments |  |
| no restart after the reverse test | src/layered_map.cpp | killed | Sprt.ShippedPairNeedsFiveAllMissWindowsToDemoteAndOneMissNever |
| demote one step early | src/layered_map.cpp | killed | Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce, Sprt.GraduatedCellIsDemotedByTheReverseTestAfterTheDerivedMissCount |
| demote direction | src/layered_map.cpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence, LayeredMap.RevisitLoopWall8of48StaysStatic |
| reject from n > T | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences, Sprt.AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen |
| reject before the gate | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| reject at l < B | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.InSampleKittiReplayOfTheAdvice, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences |
| graduated cells can be rejected | src/layered_map.cpp | survived; EQUIVALENT: equivalent: a graduated cell has l >= ceiling - A = 2 l_hit > 0 > B, so l <= B never holds for it |  |
| keep non-candidates | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.InSampleKittiReplayOfTheAdvice, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences |
| no periodic-candidate guard | src/layered_map.cpp | killed | Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic, Sprt.PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone |
| periodicity history kept after rejection | src/layered_map.cpp | killed | Sprt.RejectionAlsoDropsThePeriodicityHistoryOfTheCell |
| gate T + 1 | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences, Sprt.AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen |
| gate 1 | src/layered_map.cpp | killed | ReplayP1.AdvisoryTableThroughLayeredMapOnTheCalibrationSequences, ReplayP1.ShippedPairOnTheOneSecondCalibrationSequences, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| Unknown from n > T | src/layered_map.cpp | killed | Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| Unknown at l < B | src/layered_map.cpp | killed | Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| Unknown with // | src/layered_map.cpp | killed | Sprt.AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen, Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic, Sprt.NMaxZeroNeverReadsOutIntermittent |
| Intermittent from n > n_max | src/layered_map.cpp | killed | Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted, Sprt.UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate, Sprt.Voxel3DBackendCountsIntermittentCells |
| n_max 0 means never | src/layered_map.cpp | killed | Sprt.AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen, Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain, Sprt.NMaxZeroNeverReadsOutIntermittent |
| Intermittent with // | src/layered_map.cpp | killed | Sprt.AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen, Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| rejected periodic candidate reads Transient | src/layered_map.cpp | killed | Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| no count restart at demotion | src/layered_map.cpp | killed | Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| intermittentCells wrong class | src/layered_map.cpp | killed | Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted, Sprt.NMaxZeroNeverReadsOutIntermittent, Sprt.UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate |
| observations() off by one | src/layered_map.cpp | killed | Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce, Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| default model legacy | include/strata_core/layered_map.hpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, LayeredMap.PeriodicDemotionNeedsContradictingFreeEvidence, Sprt.StructDefaultsAreTheShippedSprtPair |
| default m0 | include/strata_core/layered_map.hpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, Sprt.StructDefaultsAreTheShippedSprtPair, ctest:defaults_match_yaml_and_prereg |
| default m1 | include/strata_core/layered_map.hpp | killed | Sprt.StructDefaultsAreTheShippedSprtPair, ctest:defaults_match_yaml_and_prereg |
| default alpha | include/strata_core/layered_map.hpp | killed | LayeredMap.AlwaysHitWallNeverPeriodicShippedDefaultsT24, Sprt.StructDefaultsAreTheShippedSprtPair, ctest:defaults_match_yaml_and_prereg |
| default beta | include/strata_core/layered_map.hpp | killed | Sprt.StructDefaultsAreTheShippedSprtPair, ctest:defaults_match_yaml_and_prereg |
| default n_max_undecided | include/strata_core/layered_map.hpp | killed | Sprt.StructDefaultsAreTheShippedSprtPair, ctest:defaults_match_yaml_and_prereg |
| Intermittent render direction | src/grid2d_backend.cpp | killed | Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| Intermittent not rendered | src/grid2d_backend.cpp | killed | Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| Grid2D count | include/strata_core/grid2d_backend.hpp | killed | Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| Voxel3D count | include/strata_core/voxel3d_backend.hpp | killed | Sprt.Voxel3DBackendCountsIntermittentCells |
| legacy: no survival decay | src/layered_map.cpp | survived by the gtests; KILLED by `check_legacy_equivalence.sh`: 10 of 19 CSVs differ (`legacy_equivalence.txt`) |  |
| legacy: miss adds l_hit | src/layered_map.cpp | killed | Grid2DBackend.CloseWindowsClosesRegardlessOfTickCount, Grid2DBackend.FreeSurvivesPruning, Grid2DBackend.FreedTransientRendersFree |

## defaults: 18 mutants, 18 killed, 0 survived, 0 build errors

| mutant | where | result | killed by (first) |
|---|---|---|---|
| grid2d m0 | strata/params/grid2d.yaml | killed | - grid2d.yaml m0: 0.25 but the library default is 0.20 |
| grid2d m1 | strata/params/grid2d.yaml | killed | - grid2d.yaml m1: 0.65 but the library default is 0.70 |
| grid2d alpha | strata/params/grid2d.yaml | killed | - grid2d.yaml alpha: 0.02 but the library default is 0.01 |
| grid2d beta | strata/params/grid2d.yaml | killed | - grid2d.yaml beta: 0.02 but the library default is 0.01 |
| grid2d n_max_undecided | strata/params/grid2d.yaml | killed | - grid2d.yaml n_max_undecided: 64 but the library default is 80 |
| grid2d evidence_model | strata/params/grid2d.yaml | killed | - grid2d.yaml evidence_model is "legacy", shipped default is "sprt" |
| grid2d calibration label | strata/params/grid2d.yaml | killed | - grid2d.yaml evidence_calibration must be "measured_2d" |
| voxel3d m0 | strata/params/voxel3d.yaml | killed | - voxel3d.yaml m0: 0.30 but the library default is 0.20 |
| voxel3d calibration label | strata/params/voxel3d.yaml | killed | - voxel3d.yaml evidence_calibration must be "fallback_2d_pair" while the 3D status is 'NOT FROZEN' |
| grid2d legacy l_hit against the library | strata/params/grid2d.yaml | killed | - grid2d.yaml l_hit: 0.9 but the library default is 0.85 |
| voxel3d period_windows against the library | strata/params/voxel3d.yaml | killed | - voxel3d.yaml period_windows: 12 but the library default is 24 |
| grid2d loses grid_origin_x (an error made once while editing this file) | strata/params/grid2d.yaml | killed | - grid2d.yaml lacks "grid_origin_x", which the node declares |
| voxel3d key the node does not declare | strata/params/voxel3d.yaml | killed | - voxel3d.yaml lacks "n_harmonics", which the node declares |
| PREREG m0 | paper/experiments/realdata/PREREG.json | killed | - grid2d.yaml m0: 0.20 but PREREG.json (not frozen) says 0.25 |
| PREREG n_max_undecided | paper/experiments/realdata/PREREG.json | killed | - grid2d.yaml n_max_undecided: 80 but PREREG.json (not frozen) says 96 |
| node m0 from a literal | strata/src/mapping_node.cpp | killed | - mapping_node.cpp does not declare "m0" from the library default def.m0 |
| node evidence_model default | strata/src/mapping_node.cpp | killed | - mapping_node.cpp does not declare evidence_model with the default "sprt" |
| library m1 against YAML and PREREG | strata_core/include/strata_core/layered_map.hpp | killed | - grid2d.yaml m1: 0.70 but the library default is 0.65 |

## tests: 249 mutants, 141 killed, 108 survived, 0 build errors

| mutant | where | result | killed by (first) |
|---|---|---|---|
| line 111: 0 -> 1 | EXPECT_EQ(active, 0); | killed | 1, Sprt.OperatingCharacteristicMatchesTheExactLatticeComputation |
| line 154: 3.5 -> 5.251 | EXPECT_NEAR(d.l_hit, std::log(3.5), 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 154: 12 -> 13 | EXPECT_NEAR(d.l_hit, std::log(3.5), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 155: 0.3 -> 0.45099999999999996 | EXPECT_NEAR(d.l_miss, std::log(0.3 / 0.8), 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 155: 0.8 -> 1.201 | EXPECT_NEAR(d.l_miss, std::log(0.3 / 0.8), 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 155: 12 -> 13 | EXPECT_NEAR(d.l_miss, std::log(0.3 / 0.8), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 156: 99.0 -> 148.501 | EXPECT_NEAR(d.A, std::log(99.0), 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 156: 12 -> 13 | EXPECT_NEAR(d.A, std::log(99.0), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 157: 99.0 -> 148.501 | EXPECT_NEAR(d.B, -std::log(99.0), 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 157: 12 -> 13 | EXPECT_NEAR(d.B, -std::log(99.0), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 158: 2 -> 3 | EXPECT_NEAR(d.ceiling, d.A + 2 * d.l_hit, 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 158: 12 -> 13 | EXPECT_NEAR(d.ceiling, d.A + 2 * d.l_hit, 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 159: 2 -> 3 | EXPECT_NEAR(d.demote_level, 2 * d.l_hit, 1e-12); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 159: 12 -> 13 | EXPECT_NEAR(d.demote_level, 2 * d.l_hit, 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 160: 0.4391263739 -> 0.65968956085 | EXPECT_NEAR(d.m_star, 0.4391263739, 1e-9); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 160: 9 -> 10 | EXPECT_NEAR(d.m_star, 0.4391263739, 1e-9); | survived (tolerance exponent) |  |
| line 161: 4 -> 5 | EXPECT_EQ(d.latency, 4); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 162: 5 -> 6 | EXPECT_EQ(d.demote_misses, 5); | killed | 1, Sprt.DerivedQuantitiesOfTheShippedPair |
| line 167: 1.466 -> 2.1999999999999997 | EXPECT_NEAR(a.l_hit, 1.466, 5e-4);  EXPECT_NEAR(a.l_miss, -0.887, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 167: 4 -> 5 | EXPECT_NEAR(a.l_hit, 1.466, 5e-4);  EXPECT_NEAR(a.l_miss, -0.887, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 167: 0.887 -> 1.3315 | EXPECT_NEAR(a.l_hit, 1.466, 5e-4);  EXPECT_NEAR(a.l_miss, -0.887, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 167: 4 -> 5 | EXPECT_NEAR(a.l_hit, 1.466, 5e-4);  EXPECT_NEAR(a.l_miss, -0.887, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 168: 0.377 -> 0.5665 | EXPECT_NEAR(a.m_star, 0.377, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 168: 4 -> 5 | EXPECT_NEAR(a.m_star, 0.377, 5e-4); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 169: 4 -> 5 | EXPECT_EQ(a.latency, 4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 170: 6 -> 7 | EXPECT_EQ(a.demote_misses, 6);                          // "about 6 all-miss windows" | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 172: 2.944 -> 4.417000000000001 | EXPECT_NEAR(b.A, 2.944, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 172: 4 -> 5 | EXPECT_NEAR(b.A, 2.944, 5e-4); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 173: 3 -> 4 | EXPECT_EQ(b.latency, 3); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 175: 7 -> 8 | EXPECT_EQ(c.latency, 7); | killed | 1, Sprt.DerivedQuantitiesOfThePairsInTheAdvice |
| line 181: 0.98 -> 1.4709999999999999 | EXPECT_NEAR(d.A, std::log(0.98 / 0.001), 1e-12); | killed | 1, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| line 181: 0.001 -> 0.0025 | EXPECT_NEAR(d.A, std::log(0.98 / 0.001), 1e-12); | killed | 1, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| line 181: 12 -> 13 | EXPECT_NEAR(d.A, std::log(0.98 / 0.001), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 182: 0.02 -> 0.031 | EXPECT_NEAR(d.B, std::log(0.02 / 0.999), 1e-12); | killed | 1, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| line 182: 0.999 -> 1.4994999999999998 | EXPECT_NEAR(d.B, std::log(0.02 / 0.999), 1e-12); | killed | 1, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| line 182: 12 -> 13 | EXPECT_NEAR(d.B, std::log(0.02 / 0.999), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 183: 0.0 -> 0.001 | EXPECT_LT(d.B, 0.0); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 184: 2 -> 3 | EXPECT_NEAR(d.ceiling, d.A + 2 * d.l_hit, 1e-12); | killed | 1, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| line 184: 12 -> 13 | EXPECT_NEAR(d.ceiling, d.A + 2 * d.l_hit, 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 190: 0.0 -> 0.001 | EXPECT_NEAR(c.demote_level, 0.0, 1e-12); | killed | 1, Sprt.ThresholdsOfUnequalErrorRatesAndACustomCeiling |
| line 190: 12 -> 13 | EXPECT_NEAR(c.demote_level, 0.0, 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 191: 12 -> 13 | EXPECT_EQ(c.demote_misses, static_cast<int>(std::ceil(d.A / -c.l_miss - 1e-12))); | survived (tolerance exponent) |  |
| line 197: 0.20 -> 0.30100000000000005 | EXPECT_EQ(p.m0, 0.20); EXPECT_EQ(p.m1, 0.70); | killed | 1, Sprt.StructDefaultsAreTheShippedSprtPair |
| line 197: 0.70 -> 1.0509999999999997 | EXPECT_EQ(p.m0, 0.20); EXPECT_EQ(p.m1, 0.70); | killed | 1, Sprt.StructDefaultsAreTheShippedSprtPair |
| line 198: 0.01 -> 0.016 | EXPECT_EQ(p.alpha, 0.01); EXPECT_EQ(p.beta, 0.01); | killed | 1, Sprt.StructDefaultsAreTheShippedSprtPair |
| line 198: 0.01 -> 0.016 | EXPECT_EQ(p.alpha, 0.01); EXPECT_EQ(p.beta, 0.01); | killed | 1, Sprt.StructDefaultsAreTheShippedSprtPair |
| line 200: 80 -> 81 | EXPECT_EQ(p.n_max_undecided, 80); | killed | 1, Sprt.StructDefaultsAreTheShippedSprtPair |
| line 201: 4 -> 5 | EXPECT_EQ(LayeredMap(p).sprt().latency, 4); | killed | 1, Sprt.StructDefaultsAreTheShippedSprtPair |
| line 207: 0.01 -> 0.016 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.01; }))); | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 208: 0.0 -> 0.001 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.0; })), std::invalid_argument); | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 209: 0.1 -> 0.15100000000000002 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = -0.1; })), std::invalid_argument); | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 210: 0.7 -> 1.0509999999999997 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.7; })), std::invalid_argument);   //  | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 211: 0.8 -> 1.201 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.8; })), std::invalid_argument);   //  | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 212: 0.99 -> 1.4859999999999998 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 0.99; }))); | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 213: 1.0 -> 1.501 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 1.0; })), std::invalid_argument); | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 214: 1.5 -> 2.251 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 1.5; })), std::invalid_argument); | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 215: 0.2 -> 0.30100000000000005 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 0.2; })), std::invalid_argument);   //  | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 217: 0.0 -> 0.001 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.0; })), std::invalid_argument); | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 218: 0.0 -> 0.001 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.beta = 0.0; })), std::invalid_argument); | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 219: 0.01 -> 0.016 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = -0.01; })), std::invalid_argument); | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 220: 0.5 -> 0.751 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.5; p.beta = 0.49; }))); | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 220: 0.49 -> 0.736 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.5; p.beta = 0.49; }))); | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 221: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.5; p.beta = 0.5; })), std::invalid | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 221: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.5; p.beta = 0.5; })), std::invalid | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 222: 0.6 -> 0.9009999999999999 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.6; p.beta = 0.5; })), std::invalid | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 222: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.6; p.beta = 0.5; })), std::invalid | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 230: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-9; })), std::inval | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 230: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-9; })), std::inval | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 230: 9 -> 10 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-9; })), std::inval | survived (tolerance exponent) |  |
| line 231: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = std::nextafter(0.5, 1.0); } | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 231: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = std::nextafter(0.5, 1.0); } | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 231: 1.0 -> 1.501 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = std::nextafter(0.5, 1.0); } | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 232: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-6; })), std::inval | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 232: 0.5 -> 0.751 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-6; })), std::inval | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 232: 6 -> 7 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-6; })), std::inval | survived (tolerance exponent) |  |
| line 234: 0.1 -> 0.15100000000000002 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.1; p.m1 = 0.1 + 2e-6; })), std::inval | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 234: 0.1 -> 0.15100000000000002 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.1; p.m1 = 0.1 + 2e-6; })), std::inval | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 234: 6 -> 7 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.1; p.m1 = 0.1 + 2e-6; })), std::inval | survived (tolerance exponent) |  |
| line 235: 0.5 -> 0.751 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 2e-5; })));   // 1 | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 235: 0.5 -> 0.751 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 2e-5; })));   // 1 | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 235: 5 -> 6 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 2e-5; })));   // 1 | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 236: 0.5 -> 0.751 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-4; })));   // 2 | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 236: 0.5 -> 0.751 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-4; })));   // 2 | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 236: 4 -> 5 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-4; })));   // 2 | survived (tolerance exponent) |  |
| line 238: 0 -> 1 | EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.n_max_undecided = 0; }))); | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 239: 1 -> 2 | EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.n_max_undecided = -1; })), std::invalid_argu | survived (input value: the mutant is still invalid (or still valid), the boundary cases are separate lines) |  |
| line 243: 6 -> 7 | EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = A - 1e-6; })), std::invalid_ | survived (tolerance exponent) |  |
| line 245: 6.0 -> 9.001 | EXPECT_NEAR(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = 6.0; })).sprt().ceiling, 6.0, | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 245: 6.0 -> 9.001 | EXPECT_NEAR(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = 6.0; })).sprt().ceiling, 6.0, | killed | 1, Sprt.ValidationRejectsEachBadInputOnBothSides |
| line 245: 0 -> 1 | EXPECT_NEAR(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = 6.0; })).sprt().ceiling, 6.0, | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 265: 12 -> 13 | EXPECT_EQ(d.latency, static_cast<int>(std::ceil(d.A / d.l_hit - 1e-12))) | survived (tolerance exponent) |  |
| line 271: 7 -> 8 | EXPECT_FALSE(m.isStatic(7)) << "window " << w << " case " << c.m0 << "/" << c.m1; | survived (cell id: the same cell is asserted on in other lines) |  |
| line 273: 7 -> 8 | EXPECT_TRUE(m.isStatic(7)) << "window " << w << " case " << c.m0 << "/" << c.m1; | killed | 1, Sprt.AlwaysHitWallGraduatesAtTheDerivedWindowAndNeverEarlier |
| line 275: 7 -> 8 | EXPECT_NE(m.classify(7), CellClass::Periodic) << "window " << w; | survived (cell id: the same cell is asserted on in other lines) |  |
| line 284: 1 -> 2 | EXPECT_FALSE(m.isStatic(1)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 286: 1 -> 2 | EXPECT_TRUE(m.isStatic(1)); | killed | 1, Sprt.ThreeHitsDoNotGraduateAtTheShippedPair |
| line 287: 1 -> 2 | EXPECT_EQ(m.classify(1), CellClass::Static); | killed | 1, Sprt.ThreeHitsDoNotGraduateAtTheShippedPair |
| line 296: 3 -> 4 | EXPECT_FALSE(m.isStatic(3)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 297: 3 -> 4 | EXPECT_EQ(m.observations(3), 10); | killed | 1, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| line 297: 10 -> 11 | EXPECT_EQ(m.observations(3), 10); | killed | 1, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| line 298: 3 -> 4 | EXPECT_LT(m.occupancyProb(3), 1e-4);                      // l = -9.81, unclamped | survived (cell id: the same cell is asserted on in other lines) |  |
| line 298: 4 -> 5 | EXPECT_LT(m.occupancyProb(3), 1e-4);                      // l = -9.81, unclamped | killed | 1, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| line 302: 3 -> 4 | EXPECT_TRUE(m.isStatic(3)); | killed | 1, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| line 303: 3 -> 4 | EXPECT_EQ(m.observations(3), 22); | killed | 1, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| line 303: 22 -> 23 | EXPECT_EQ(m.observations(3), 22); | killed | 1, Sprt.ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate |
| line 312: 20000 -> 20001 | ASSERT_GT(h.histories, 20000); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 314: 0.01 -> 0.016 | EXPECT_LE(wilsonUpper(h.ever_static, h.histories), 0.01) << h.ever_static << " of " << h.histories < | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 315: 0 -> 1 | EXPECT_GT(h.ever_static, 0);                                 // the test can see graduations at all | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 320: 0.002 -> 0.004 | EXPECT_LE(wilsonUpper(h.ever_static, h.histories), 0.002) << h.ever_static << " of " << h.histories; | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 326: 0.01 -> 0.016 | EXPECT_LE(wilsonUpper(h.ever_static, h.histories), 0.01) << h.ever_static << " of " << h.histories; | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 333: 0.01 -> 0.016 | EXPECT_GT(wilsonLower(h.ever_static, h.histories), 0.01) << h.ever_static << " of " << h.histories; | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 348: 4.5 -> 6.751 | EXPECT_NEAR(mc, exact, 4.5 * se) << "gate " << gate << " m = " << m << " exact " << exact << " mc "  | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 357: 0.025 -> 0.038500000000000006 | EXPECT_NEAR(ex, w, 0.025) << "m = " << m << " exact " << ex << " wald " << w; | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 360: 0.20 -> 0.30100000000000005 | EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01)); | killed | 1, Sprt.ExactOcAgainstWaldDiffersByTheOvershootOnly |
| line 360: 0.20 -> 0.30100000000000005 | EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01)); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 360: 0.20 -> 0.30100000000000005 | EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01)); | killed | 1, Sprt.ExactOcAgainstWaldDiffersByTheOvershootOnly |
| line 360: 0.70 -> 1.0509999999999997 | EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01)); | killed | 1, Sprt.ExactOcAgainstWaldDiffersByTheOvershootOnly |
| line 360: 0.01 -> 0.016 | EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01)); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 360: 0.01 -> 0.016 | EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01)); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 361: 0.20 -> 0.30100000000000005 | EXPECT_LE(exactOcGraduate(0.20, d), 0.01); | killed | 1, Sprt.ExactOcAgainstWaldDiffersByTheOvershootOnly |
| line 361: 0.01 -> 0.016 | EXPECT_LE(exactOcGraduate(0.20, d), 0.01); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 362: 0.70 -> 1.0509999999999997 | EXPECT_GE(exactOcGraduate(0.70, d), 0.99);                       // power at m1 | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 362: 0.99 -> 1.4859999999999998 | EXPECT_GE(exactOcGraduate(0.70, d), 0.99);                       // power at m1 | killed | 1, Sprt.ExactOcAgainstWaldDiffersByTheOvershootOnly |
| line 368: 1.0 -> 1.501 | EXPECT_NEAR(exactOcGraduate(1.0, d), 1.0, 1e-12); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 368: 1.0 -> 1.501 | EXPECT_NEAR(exactOcGraduate(1.0, d), 1.0, 1e-12); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 368: 12 -> 13 | EXPECT_NEAR(exactOcGraduate(1.0, d), 1.0, 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 369: 0.0 -> 0.001 | EXPECT_NEAR(exactOcGraduate(0.0, d), 0.0, 1e-12); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 369: 0.0 -> 0.001 | EXPECT_NEAR(exactOcGraduate(0.0, d), 0.0, 1e-12); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 369: 12 -> 13 | EXPECT_NEAR(exactOcGraduate(0.0, d), 0.0, 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 372: 0.20 -> 0.30100000000000005 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 372: 0.20 -> 0.30100000000000005 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 372: 0.70 -> 1.0509999999999997 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 372: 0.01 -> 0.016 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 372: 0.01 -> 0.016 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | survived (Wald identity: OC(m0) = alpha and OC(m1) = 1 - beta do not depend on the other error rate or on the third argument's position) |  |
| line 372: 0.01 -> 0.016 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 372: 9 -> 10 | EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 373: 0.70 -> 1.0509999999999997 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 373: 0.20 -> 0.30100000000000005 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | survived (Wald identity: OC(m0) = alpha and OC(m1) = 1 - beta do not depend on the other error rate or on the third argument's position) |  |
| line 373: 0.70 -> 1.0509999999999997 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 373: 0.01 -> 0.016 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | survived (Wald identity: OC(m0) = alpha and OC(m1) = 1 - beta do not depend on the other error rate or on the third argument's position) |  |
| line 373: 0.01 -> 0.016 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 373: 0.99 -> 1.4859999999999998 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | killed | 1, Sprt.ExactOcLatticeComputationSelfCheck |
| line 373: 9 -> 10 | EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9); | survived (tolerance exponent) |  |
| line 385: 5 -> 6 | EXPECT_EQ(m.classify(5), CellClass::Transient) << "T=" << T << " n=" << n; | killed | 1, Sprt.AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen |
| line 389: 5 -> 6 | EXPECT_EQ(m.classify(5), CellClass::Unknown); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 409: 9 -> 10 | EXPECT_GE(m.occupancyProb(9), 1.0 / (1.0 + std::exp(-m.sprt().B)) - 1e-12) << "window " << (w + 1); | killed | 1, Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| line 409: 1.0 -> 1.501 | EXPECT_GE(m.occupancyProb(9), 1.0 / (1.0 + std::exp(-m.sprt().B)) - 1e-12) << "window " << (w + 1); | killed | 1, Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| line 409: 1.0 -> 1.501 | EXPECT_GE(m.occupancyProb(9), 1.0 / (1.0 + std::exp(-m.sprt().B)) - 1e-12) << "window " << (w + 1); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 409: 12 -> 13 | EXPECT_GE(m.occupancyProb(9), 1.0 / (1.0 + std::exp(-m.sprt().B)) - 1e-12) << "window " << (w + 1); | survived (tolerance exponent) |  |
| line 409: 1 -> 2 | EXPECT_GE(m.occupancyProb(9), 1.0 / (1.0 + std::exp(-m.sprt().B)) - 1e-12) << "window " << (w + 1); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 415: 9 -> 10 | EXPECT_EQ(m.classify(9), CellClass::Unknown) << "window " << (w + 1); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 415: 1 -> 2 | EXPECT_EQ(m.classify(9), CellClass::Unknown) << "window " << (w + 1); | survived (literal in the failure message, not in an assertion) |  |
| line 424: 0 -> 1 | EXPECT_GT(candidate_unknown_checks, 0);                      // the branch was reached, not skipped | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 425: 9 -> 10 | EXPECT_EQ(m.classify(9), CellClass::Periodic); | killed | 1, Sprt.DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic |
| line 426: 9 -> 10 | EXPECT_FALSE(m.isStatic(9)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 437: 2 -> 3 | EXPECT_GT(m.periodicProb(2), 0.99); | killed | 1, Sprt.RejectionAlsoDropsThePeriodicityHistoryOfTheCell |
| line 437: 0.99 -> 1.4859999999999998 | EXPECT_GT(m.periodicProb(2), 0.99); | killed | 1, Sprt.RejectionAlsoDropsThePeriodicityHistoryOfTheCell |
| line 446: 2 -> 3 | EXPECT_EQ(m.observations(2), 1);                                // a new history | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 446: 1 -> 2 | EXPECT_EQ(m.observations(2), 1);                                // a new history | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 447: 2 -> 3 | EXPECT_NEAR(m.occupancyProb(2), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 447: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(2), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 447: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(2), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 447: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(2), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 449: 2 -> 3 | EXPECT_TRUE(m.isStatic(2));                                     // graduates after the derived laten | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 450: 2 -> 3 | EXPECT_EQ(m.observations(2), m.sprt().latency); | killed | 1, Sprt.RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory |
| line 460: 4 -> 5 | ASSERT_TRUE(m.isStatic(4)); | killed | 1, Sprt.GraduatedCellIsDemotedByTheReverseTestAfterTheDerivedMissCount |
| line 477: 4 -> 5 | ASSERT_TRUE(m.isStatic(4)); | killed | 1, Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| line 478: 4 -> 5 | EXPECT_NEAR(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | killed | 1, Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| line 478: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | killed | 1, Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| line 478: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | killed | 1, Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| line 478: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 480: 4 -> 5 | ASSERT_TRUE(m.isStatic(4)) << "miss " << k - 1; | killed | 1, Sprt.GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt |
| line 480: 1 -> 2 | ASSERT_TRUE(m.isStatic(4)) << "miss " << k - 1; | survived (literal in the failure message, not in an assertion) |  |
| line 483: 4 -> 5 | EXPECT_FALSE(m.isStatic(4)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 502: 6 -> 7 | EXPECT_NEAR(m.occupancyProb(6), 1.0 / (1.0 + std::exp(-l)), 1e-9) << "window " << (w + 1); | killed | 1, Sprt.PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone |
| line 502: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(6), 1.0 / (1.0 + std::exp(-l)), 1e-9) << "window " << (w + 1); | killed | 1, Sprt.PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone |
| line 502: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(6), 1.0 / (1.0 + std::exp(-l)), 1e-9) << "window " << (w + 1); | killed | 1, Sprt.PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone |
| line 502: 9 -> 10 | EXPECT_NEAR(m.occupancyProb(6), 1.0 / (1.0 + std::exp(-l)), 1e-9) << "window " << (w + 1); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 502: 1 -> 2 | EXPECT_NEAR(m.occupancyProb(6), 1.0 / (1.0 + std::exp(-l)), 1e-9) << "window " << (w + 1); | survived (logistic constant inside the expected value) |  |
| line 503: 6 -> 7 | EXPECT_FALSE(m.isStatic(6)) << "window " << (w + 1); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 503: 1 -> 2 | EXPECT_FALSE(m.isStatic(6)) << "window " << (w + 1); | survived (literal in the failure message, not in an assertion) |  |
| line 512: 100 -> 101 | EXPECT_GT(checked, 100); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 532: 4 -> 5 | ASSERT_TRUE(m.isStatic(4)) << "cycle " << cycle; | killed | 1, Sprt.ShippedPairNeedsFiveAllMissWindowsToDemoteAndOneMissNever |
| line 536: 4 -> 5 | EXPECT_FALSE(m.isStatic(4));                                       // the fifth | survived (cell id: the same cell is asserted on in other lines) |  |
| line 537: 4 -> 5 | EXPECT_NEAR(m.occupancyProb(4), 0.5, 1e-12);                       // restarted at l = 0 | killed | 1, Sprt.ShippedPairNeedsFiveAllMissWindowsToDemoteAndOneMissNever |
| line 537: 0.5 -> 0.751 | EXPECT_NEAR(m.occupancyProb(4), 0.5, 1e-12);                       // restarted at l = 0 | killed | 1, Sprt.ShippedPairNeedsFiveAllMissWindowsToDemoteAndOneMissNever |
| line 537: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(4), 0.5, 1e-12);                       // restarted at l = 0 | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 544: 4 -> 5 | ASSERT_FALSE(m.isStatic(4)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 545: 4 -> 5 | ASSERT_EQ(m.classify(4), CellClass::Transient); | killed | 1, Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain |
| line 547: 4 -> 5 | EXPECT_TRUE(m.isStatic(4)); | killed | 1, Sprt.DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain |
| line 557: 4 -> 5 | ASSERT_TRUE(m.isStatic(4)); | killed | 1, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| line 559: 4 -> 5 | ASSERT_FALSE(m.isStatic(4)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 560: 4 -> 5 | EXPECT_EQ(m.observations(4), 0); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 560: 0 -> 1 | EXPECT_EQ(m.observations(4), 0); | killed | 1, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| line 561: 4 -> 5 | EXPECT_EQ(m.classify(4), CellClass::Transient); | killed | 1, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| line 563: 4 -> 5 | EXPECT_EQ(m.observations(4), 1); | killed | 1, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| line 563: 1 -> 2 | EXPECT_EQ(m.observations(4), 1); | killed | 1, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| line 564: 4 -> 5 | EXPECT_EQ(m.classify(4), CellClass::Transient); | killed | 1, Sprt.DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce |
| line 568: 4 -> 5 | ASSERT_LT(m.observations(4), T); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 569: 4 -> 5 | EXPECT_LT(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().B)));   // below sigma(B): not floored | survived (cell id: the same cell is asserted on in other lines) |  |
| line 569: 1.0 -> 1.501 | EXPECT_LT(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().B)));   // below sigma(B): not floored | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 569: 1.0 -> 1.501 | EXPECT_LT(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().B)));   // below sigma(B): not floored | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 570: 4 -> 5 | EXPECT_NE(m.classify(4), CellClass::Intermittent); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 588: 6 -> 7 | EXPECT_EQ(m.classify(6), CellClass::Periodic); | killed | 1, Sprt.DoorGraduatesEarlyAndPeriodicityThenDemotesIt |
| line 589: 6 -> 7 | EXPECT_FALSE(m.isStatic(6)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 607: 8 -> 9 | EXPECT_EQ(m.classify(8), CellClass::Transient) << "n = " << n; | killed | 1, Sprt.UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate |
| line 610: 8 -> 9 | EXPECT_EQ(m.classify(8), CellClass::Intermittent); | killed | 1, Sprt.UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate |
| line 613: 8 -> 9 | EXPECT_FALSE(m.isStatic(8)); | survived (cell id: the same cell is asserted on in other lines) |  |
| line 615: 8 -> 9 | EXPECT_TRUE(m.isStatic(8)); | killed | 1, Sprt.UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate |
| line 616: 8 -> 9 | EXPECT_EQ(m.classify(8), CellClass::Static);           // Static wins over Intermittent | killed | 1, Sprt.UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate |
| line 623: 8 -> 9 | EXPECT_EQ(m.classify(8), CellClass::Transient); | killed | 1, Sprt.NMaxZeroNeverReadsOutIntermittent |
| line 633: 6 -> 7 | ASSERT_GE(m.observations(6), 48); | killed | 1, Sprt.PeriodicWinsOverIntermittent |
| line 633: 48 -> 49 | ASSERT_GE(m.observations(6), 48); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 634: 6 -> 7 | EXPECT_EQ(m.classify(6), CellClass::Periodic); | killed | 1, Sprt.PeriodicWinsOverIntermittent |
| line 655: 0.5 -> 0.751 | EXPECT_GT(b.layered().occupancyProb(c), 0.5); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 659: 50 -> 51 | EXPECT_EQ(g.data[c], 50); | killed | 1, Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| line 660: 3 -> 4 | ASSERT_LT(b.layered().occupancyProb(gridCellId(gm, 3, 0)), 0.5); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 660: 0 -> 1 | ASSERT_LT(b.layered().occupancyProb(gridCellId(gm, 3, 0)), 0.5); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 660: 0.5 -> 0.751 | ASSERT_LT(b.layered().occupancyProb(gridCellId(gm, 3, 0)), 0.5); | survived (one-sided bound moved away from the data: a looser bound passes by design) |  |
| line 661: 3 -> 4 | ASSERT_EQ(b.layered().classify(gridCellId(gm, 3, 0)), CellClass::Intermittent); | survived (cell coordinate or exponent inside an expression) |  |
| line 661: 0 -> 1 | ASSERT_EQ(b.layered().classify(gridCellId(gm, 3, 0)), CellClass::Intermittent); | killed | 1, Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| line 662: 3 -> 4 | EXPECT_EQ(g.data[gridCellId(gm, 3, 0)], 0); | survived (cell coordinate or exponent inside an expression) |  |
| line 662: 0 -> 1 | EXPECT_EQ(g.data[gridCellId(gm, 3, 0)], 0); | killed | 1, Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| line 662: 0 -> 1 | EXPECT_EQ(g.data[gridCellId(gm, 3, 0)], 0); | killed | 1, Sprt.IntermittentCellIsRenderedLikeTransientInTheGridAndCounted |
| line 670: 1 -> 2 | EXPECT_EQ(m.occupancyProb(1), 0.0);                                    // unknown cell | survived (cell id: the same cell is asserted on in other lines) |  |
| line 670: 0.0 -> 0.001 | EXPECT_EQ(m.occupancyProb(1), 0.0);                                    // unknown cell | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 672: 1 -> 2 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.l_hit)), 1e-12); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 672: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.l_hit)), 1e-12); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 672: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.l_hit)), 1e-12); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 672: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.l_hit)), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 674: 1 -> 2 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-(d.l_hit + d.l_miss))), 1e-12); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 674: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-(d.l_hit + d.l_miss))), 1e-12); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 674: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-(d.l_hit + d.l_miss))), 1e-12); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 674: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-(d.l_hit + d.l_miss))), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 676: 1 -> 2 | ASSERT_TRUE(m.isStatic(1)); | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 677: 1 -> 2 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.ceiling)), 1e-12);   // restarted at the ce | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 677: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.ceiling)), 1e-12);   // restarted at the ce | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 677: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.ceiling)), 1e-12);   // restarted at the ce | killed | 1, Sprt.OccupancyProbIsTheLogisticOfTheStatistic |
| line 677: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.ceiling)), 1e-12);   // restarted at the ce | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 683: 1 -> 2 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | killed | 1, Sprt.HitWinsOverMissInTheSameWindow |
| line 683: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | killed | 1, Sprt.HitWinsOverMissInTheSameWindow |
| line 683: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | killed | 1, Sprt.HitWinsOverMissInTheSameWindow |
| line 683: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 690: 1 -> 2 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | killed | 1, Sprt.CeilingCapsTheStatisticOfAnUngraduatedPeriodicCandidate |
| line 690: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | killed | 1, Sprt.CeilingCapsTheStatisticOfAnUngraduatedPeriodicCandidate |
| line 690: 1.0 -> 1.501 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | killed | 1, Sprt.CeilingCapsTheStatisticOfAnUngraduatedPeriodicCandidate |
| line 690: 12 -> 13 | EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12); | survived (tolerance of an EXPECT_NEAR widened) |  |
| line 744: 1 -> 2 | EXPECT_TRUE(m.isStatic(1));                                            // v0.1: window 3 | killed | 1, Sprt.LegacyIsStillAvailableByName |
| line 745: 0 -> 1 | EXPECT_EQ(m.sprt().latency, 0); | killed | 1, Sprt.LegacyIsStillAvailableByName |


## Addendum (2026-10-18, review finding 2): defaults sweep for the diagnostics note

Three mutants were added to the `defaults` sweep of `mutation_p2.py` (`check_defaults.py` check 6) and the sweep was re-run at this head: 21 of 21 killed.
The three: the provisional note loses its text while `PREREG.json` says not frozen; the note names the wrong gate; `mapping_node.cpp` stops including `evidence_status.hpp`.
Ten mutants of `strata/include/strata/evidence_status.hpp` (user and legacy also provisional, never provisional, value or message without the note, note at the end, uncalibrated text on the wrong backend or label, `m1` value, `n_max_undecided` dropped) were run against `strata/test/test_evidence_status.cpp`: 10 of 10 killed (two survivors of the first draft, the `m1` value and the uncalibrated text on `grid2d`, got their assertions).

## Addendum (2026-10-19, Opus review findings 1 to 3 of the v2 branch)

Mutant, run by hand against `strata_core` (113 tests): in `layered_map.cpp`, `if (periodic) e.graduated = false;` also resets `log_odds = 0.0` and `observations = 0` (the reverse-test restart applied to the periodic path).
Before the fix it survived all tests. After the added assertions in `Sprt.DoorGraduatesEarlyAndPeriodicityThenDemotesIt` (at the window where `isStatic` goes from true to false, `observations()` is n + 1 and `occupancyProb` equals the sigma of the old level plus this window's update, capped at the ceiling), it is killed (3 failed assertions, 1 failed test). The file was restored and 113 of 113 passed.
`scripts/check_evidence_params.sh` was run in the ros2_humble env against the colcon build of this head: all cases passed, including four new ones for the ignored-key warning.

## Addendum (2026-10-19, Opus review finding 1 of the second v2 round): tie-break of the periodic path and the reverse test

Mutant, run by hand against `strata_core` (114 tests): in `layered_map.cpp`, `else if (e.log_odds <= d.demote_level)` becomes `if (...)`, so the reverse-test restart also runs in a window where the periodic predicate fires.
It survived all 113 earlier tests. The new test `Sprt.PeriodicPathWinsWhenTheReverseTestFiresInTheSameWindow` (door of 17 open and 7 closed windows of 24, shipped defaults; the first tie is window 45, where the cell must keep `n` = 24 and its level) kills it: 1 failed test of 114. The file was restored and 114 of 114 passed.
SPEC 3.2a now states the tie-break (the periodic path wins).
