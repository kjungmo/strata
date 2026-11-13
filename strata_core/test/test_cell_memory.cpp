// Per-cell memory: the periodicity kernel on a slot, the slot pool, the cell node. Not part of the
// window-close oracle (test_touched_close.cpp), but it uses the same frozen periodicity copy.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "strata_core/layered_map.hpp"
#include "strata_core/periodicity.hpp"
#include "test_support.hpp"

using namespace strata_core;
using namespace strata_test;

// ------------------------------------------------------------- per-cell memory (kernel, pool) ----------
// The periodicity arithmetic is a stateless kernel working on a slot of 2 + 6 H doubles; LayeredMap
// keeps the slots in a pool, PeriodicityModel in one vector per id. All paths (frozen old model,
// wrapper, kernel on a bare slot) must agree to the bit, for any n_harmonics that was accepted before.

TEST(CellMemory, KernelWrapperAndFrozenModelAgreeToTheBit) {
  for (int H : {-1, 0, 1, 2, 3, 4, 5, 8}) {
    for (int T : {1, 8, 24}) {
      const PeriodicityParams pp{T, H};
      FrozenPeriodicity frozen(pp);
      PeriodicityModel wrapper(pp);
      const PeriodicityKernel kernel(pp);
      std::vector<double> slot(kernel.slotDoubles(), 0.0);   // all zero: "no entry yet"
      const double* const zero = slot.data();
      std::vector<double> slot2(kernel.slotDoubles(), 0.0);   // the same history through a per-window phase table
      PeriodicityKernel::PhaseTable table;
      const CellId id = 77;
      Rng rng(1000 + H * 31 + T);
      auto agree = [&](const double* sl, int step) {
        const std::string tag = "H=" + std::to_string(H) + " T=" + std::to_string(T) + " step " + std::to_string(step);
        EXPECT_TRUE(sameBits(frozen.amplitude(id), wrapper.amplitude(id))) << tag;
        EXPECT_TRUE(sameBits(frozen.amplitude(id), kernel.amplitude(sl))) << tag;
        EXPECT_TRUE(sameBits(frozen.falseAlarm(id), wrapper.falseAlarm(id))) << tag;
        EXPECT_TRUE(sameBits(frozen.falseAlarm(id), kernel.falseAlarm(sl))) << tag;
        for (int w : {0, 1, 7, 8, 100, 12345}) {
          EXPECT_TRUE(sameBits(frozen.predict(id, w), wrapper.predict(id, w))) << tag;
          EXPECT_TRUE(sameBits(frozen.predict(id, w), kernel.predict(sl, w))) << tag;
        }
        for (int k = -1; k <= std::max(H, 0) + 1; ++k) {
          const HarmonicStat a = frozen.harmonic(id, k), b = wrapper.harmonic(id, k), c = kernel.harmonic(sl, k);
          for (const HarmonicStat* o : {&b, &c}) {
            EXPECT_TRUE(sameBits(a.n, o->n) && sameBits(a.amplitude, o->amplitude) &&
                        sameBits(a.dchi, o->dchi) && sameBits(a.chi0, o->chi0)) << tag << " k=" << k;
          }
        }
        for (double a_min : {0.0, 0.3, 0.9})
          for (double alpha : {0.01, 0.2, 1.0})
            for (bool spend : {false, true}) {
              const bool x = frozen.isPeriodic(id, a_min, alpha, spend);
              EXPECT_EQ(x, wrapper.isPeriodic(id, a_min, alpha, spend)) << tag;
              EXPECT_EQ(x, kernel.isPeriodic(sl, a_min, alpha, spend)) << tag;
            }
      };
      agree(nullptr, -2);                         // no entry: a null slot
      agree(zero, -1);                            // and an all-zero slot
      EXPECT_FALSE(wrapper.has(id));
      int w = 0;
      for (int step = 0; step < 120; ++step) {
        w += 1 + (rng.below(5) == 0 ? rng.below(6) : 0);
        const bool occ = (step % 16) < 7 ? rng.u() < 0.9 : rng.u() < 0.2;
        frozen.gather(id, occ, w);
        wrapper.gather(id, occ, w);
        kernel.gather(slot.data(), occ, w);
        kernel.phases(w, table);
        kernel.gather(slot2.data(), occ, table);
        ASSERT_EQ(std::memcmp(slot.data(), slot2.data(), slot.size() * sizeof(double)), 0)
            << "phase table path differs, H=" << H << " T=" << T << " step " << step;
        if (step < 12 || step % 7 == 0) agree(slot.data(), step);
      }
      EXPECT_TRUE(wrapper.has(id));
      EXPECT_EQ(wrapper.size(), 1u);
      wrapper.erase(id);
      EXPECT_FALSE(wrapper.has(id));
    }
  }
}

TEST(CellMemory, AMissingOrZeroSlotAnswersLikeACellWithoutAnEntry) {
  for (int H : {0, 2, 8}) {
    const PeriodicityParams pp{8, H};
    FrozenPeriodicity frozen(pp);
    const PeriodicityKernel kernel(pp);
    const std::vector<double> zero(kernel.slotDoubles(), 0.0);
    for (const double* sl : {static_cast<const double*>(nullptr), zero.data()}) {
      EXPECT_EQ(kernel.amplitude(sl), 0.0);
      EXPECT_FALSE(kernel.isPeriodic(sl, 0.0, 1.0));
      EXPECT_FALSE(kernel.isPeriodic(sl, 0.0, 0.2, true));
      EXPECT_EQ(kernel.falseAlarm(sl), 1.0);
      EXPECT_EQ(kernel.predict(sl, 5), 0.5);
      const HarmonicStat h = kernel.harmonic(sl, 0);
      EXPECT_EQ(h.n, 0.0); EXPECT_EQ(h.amplitude, 0.0); EXPECT_EQ(h.dchi, 0.0); EXPECT_EQ(h.chi0, 0.0);
    }
    EXPECT_EQ(frozen.amplitude(5), 0.0);
    EXPECT_FALSE(frozen.isPeriodic(5, 0.0, 1.0));
    EXPECT_EQ(frozen.falseAlarm(5), 1.0);
    EXPECT_EQ(frozen.predict(5, 5), 0.5);
  }
}

TEST(CellMemory, TheSlotPoolReusesZeroesAndGrowsInBlocks) {
  CoeffPool pool(14);
  EXPECT_EQ(pool.inUse(), 0u);
  std::vector<std::uint32_t> ids;
  for (int i = 0; i < 3 * static_cast<int>(CoeffPool::kBlockSlots) + 5; ++i) ids.push_back(pool.acquire());
  const double* first = pool.at(ids[0]);
  for (std::size_t i = 0; i < ids.size(); ++i) ASSERT_EQ(ids[i], i);   // fresh slots in order
  EXPECT_EQ(pool.inUse(), ids.size());
  EXPECT_EQ(pool.at(ids[0]), first) << "growth must not move an existing slot";
  for (std::uint32_t id : ids) for (std::size_t d = 0; d < 14; ++d) ASSERT_EQ(pool.at(id)[d], 0.0);
  for (std::uint32_t id : ids) std::fill(pool.at(id), pool.at(id) + 14, 7.0);   // dirty every slot
  pool.release(ids[10]); pool.release(ids[4000]);
  EXPECT_EQ(pool.inUse(), ids.size() - 2);
  const std::uint32_t a = pool.acquire(), b = pool.acquire();
  EXPECT_TRUE((a == ids[4000] && b == ids[10]));
  for (std::uint32_t id : {a, b}) for (std::size_t d = 0; d < 14; ++d) EXPECT_EQ(pool.at(id)[d], 0.0) << "a reused slot is zeroed";
  EXPECT_EQ(pool.inUse(), ids.size());
  EXPECT_EQ(pool.acquire(), ids.size()) << "after the free list is empty, the next fresh slot";
  // Moving the pool keeps the slots where they are.
  const double* p5 = pool.at(5);
  CoeffPool moved(std::move(pool));
  EXPECT_EQ(moved.at(5), p5);
}

TEST(CellMemory, TheMapHoldsASlotExactlyForTheCellsGatheredAndNotErased) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMapParams p = base(m, true, 1);
    LayeredMap map(p);
    EXPECT_EQ(map.coeffSlotsInUse(), 0u);
    map.observeHit(1); map.observeMiss(2);
    EXPECT_EQ(map.coeffSlotsInUse(), 0u) << "integrate allocates nothing but the small node";
    map.endWindow();
    EXPECT_EQ(map.coeffSlotsInUse(), 2u);
    // misses only: pruned after some closes, the slot goes with the cell
    map.observeHit(1);
    for (int w = 0; w < 200 && map.cellCount() > 1; ++w) { map.observeHit(1); map.observeMiss(2); map.endWindow(); }
    ASSERT_EQ(map.cellCount(), 1u);
    EXPECT_EQ(map.coeffSlotsInUse(), 1u) << "an erased cell releases its slot";
    map.observeMiss(2); map.endWindow();   // created again: a slot again, from zeros
    EXPECT_EQ(map.cellCount(), 2u);
    EXPECT_EQ(map.coeffSlotsInUse(), 2u);
    EXPECT_EQ(map.observations(2), 1);
    // periodicity off: never a slot
    LayeredMapParams q = base(m, false, 1);
    LayeredMap off(q);
    for (CellId c = 0; c < 100; ++c) off.observeHit(c);
    off.endWindow(); off.endWindow();
    EXPECT_EQ(off.coeffSlotsInUse(), 0u);
  }
}

TEST(CellMemory, TheNodeAndTheSlotHaveStatedSizes) {
  // Small node: key 8 + 24 = 32 bytes of payload. The 24 are the fields of CellEvidence and the
  // slot index (31 bits, sharing a word with `graduated`). The libstdc++ node adds the 8-byte link,
  // which is 40 bytes in one 48-byte malloc chunk: the same chunk as before the pool.
  // The slot of the default n_harmonics = 2 is 2 + 6 * 2 = 14 doubles = 112 bytes.
  static_assert(sizeof(double) == 8 && sizeof(CellId) == 8, "the sizes below assume LP64 doubles");
  EXPECT_EQ(sizeof(CellEvidence), 24u);
  EXPECT_EQ(LayeredMap::kCellPayloadBytes, 32u);   // pair<const CellId, CellState>; CellState is 24
  EXPECT_EQ(LayeredMap::kCellPayloadBytes - sizeof(CellId), 24u);
  EXPECT_EQ(LayeredMap(LayeredMapParams{}).coeffSlotBytes(), 112u);
  for (int h : {-3, 0, 1, 5, 8}) {
    LayeredMapParams p; p.periodicity.n_harmonics = h;
    EXPECT_EQ(LayeredMap(p).coeffSlotBytes(), (2u + 6u * static_cast<unsigned>(std::max(h, 0))) * 8u) << h;
  }
}

TEST(CellMemory, TheConstantCellShortcutGivesTheBitsOfTheFullComputation) {
  // FrozenPeriodicity has no shortcut: it is the full computation.
  for (int H : {-1, 0, 1, 2, 3, 5}) {
    for (int T : {1, 4, 8, 24}) {
      const PeriodicityParams pp{T, H};
      const PeriodicityKernel kernel(pp);
      for (int mode = 0; mode < 4; ++mode) {   // all hit, all miss, constant then one flip, mixed
        FrozenPeriodicity frozen(pp);
        std::vector<double> slot(kernel.slotDoubles(), 0.0);
        Rng rng(500 + H * 7 + T + mode);
        int w = 0;
        for (int step = 0; step < 90; ++step) {
          w += 1 + rng.below(3);
          bool occ = mode == 0 ? true : mode == 1 ? false : mode == 2 ? (step != 40 + (T % 3)) : rng.u() < 0.5;
          if (mode == 3 && step < 30) occ = true;   // constant for a while, then mixed
          frozen.gather(7, occ, w);
          kernel.gather(slot.data(), occ, w);
          const std::string tag = "H=" + std::to_string(H) + " T=" + std::to_string(T) + " mode " +
                                  std::to_string(mode) + " step " + std::to_string(step);
          EXPECT_TRUE(sameBits(frozen.amplitude(7), kernel.amplitude(slot.data()))) << tag;
          EXPECT_TRUE(sameBits(frozen.falseAlarm(7), kernel.falseAlarm(slot.data()))) << tag;
          EXPECT_TRUE(sameBits(frozen.predict(7, w + 1), kernel.predict(slot.data(), w + 1))) << tag;
          for (int k = 0; k < std::max(H, 0); ++k) {
            const HarmonicStat a = frozen.harmonic(7, k), b = kernel.harmonic(slot.data(), k);
            EXPECT_TRUE(sameBits(a.dchi, b.dchi) && sameBits(a.amplitude, b.amplitude)) << tag;
          }
          for (double a_min : {-1.0, -0.0, 0.0, 1e-300, 0.3, 2.0})
            for (double alpha : {0.0, 0.01, 0.2, 0.999, 1.0, 3.0})
              for (bool spend : {false, true})
                EXPECT_EQ(frozen.isPeriodic(7, a_min, alpha, spend), kernel.isPeriodic(slot.data(), a_min, alpha, spend)) << tag;
        }
      }
    }
  }
}

// ---- moved-from map, observer throw with periodicity on, pool growth failure ----------------------

TEST(CellMemory, AMovedFromMapIsAValidEmptyMapThatCanBeUsedAgain) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMapParams p = base(m, true, 1);
    LayeredMap a(p);
    a.observeHit(1); a.observeMiss(2); a.observeMiss(3); a.endWindow();
    for (int w = 0; w < 200 && a.cellCount() > 1; ++w) { a.observeHit(1); a.observeMiss(2); a.observeMiss(3); a.endWindow(); }
    ASSERT_EQ(a.cellCount(), 1u);   // some cells were pruned: the pool has a free list
    ASSERT_GT(a.coeffSlotsInUse(), 0u);
    LayeredMap b(std::move(a));
    EXPECT_EQ(a.coeffSlotsInUse(), 0u);
    EXPECT_EQ(a.cellCount(), 0u);
    a.observeHit(5);
    a.endWindow();                  // must not touch the pool of b
    EXPECT_EQ(a.cellCount(), 1u);
    EXPECT_EQ(a.coeffSlotsInUse(), 1u);
    EXPECT_EQ(a.observations(5), 1);
    EXPECT_EQ(b.cellCount(), 1u);
    EXPECT_EQ(b.observations(1) > 0, true);
    b.observeHit(1); b.endWindow();   // b still works
    LayeredMap c(p);
    c.observeHit(9); c.endWindow();
    c = std::move(b);               // move assignment: c's own pool is dropped, b is emptied
    EXPECT_EQ(b.coeffSlotsInUse(), 0u);
    b.observeHit(7); b.endWindow();
    EXPECT_EQ(b.coeffSlotsInUse(), 1u);
    EXPECT_EQ(c.cellCount(), 1u);
    EXPECT_EQ(c.coeffSlotsInUse(), 1u);
  }
}

namespace {
struct ThrowOnCall : WindowObserver {
  int throw_on{-1}, calls{0};
  void onTouchedCell(int, CellId, bool, bool) override { if (++calls == throw_on) throw std::runtime_error("observer failed"); }
};
}  // namespace

TEST(CellMemory, AnObserverThatThrowsWithPeriodicityOnGathersEachCellExactlyOnce) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    const LayeredMapParams p = base(m, true, 1);
    LayeredMap map(p);
    ThrowOnCall obs; obs.throw_on = 3; map.setWindowObserver(&obs);
    for (CellId c = 0; c < 5; ++c) map.observeHit(c);
    EXPECT_THROW(map.endWindow(), std::runtime_error);   // cells 0 and 1 done; cell 2 failed in the observer
    EXPECT_EQ(map.observations(0), 1);
    EXPECT_EQ(map.observations(2), 0);
    EXPECT_EQ(map.coeffSlotsInUse(), 3u) << "cell 2 got its slot before the observer ran; it holds zeros";
    map.endWindow();                                     // retry: cells 2, 3, 4 (window index 2)
    EXPECT_EQ(map.lastWindowTouched(), 3u);
    EXPECT_EQ(map.coeffSlotsInUse(), 5u);
    for (CellId c = 0; c < 5; ++c) EXPECT_EQ(map.observations(c), 1) << c;
    // Cells 0, 1 were gathered at window 1, cells 2..4 at window 2, each once.
    PeriodicityModel at1(p.periodicity), at2(p.periodicity);
    at1.gather(0, true, 1); at2.gather(0, true, 2);
    for (CellId c = 0; c < 5; ++c)
      EXPECT_TRUE(sameBits(map.periodicProb(c), (c < 2 ? at1 : at2).predict(0, map.windowCount()))) << c;
  }
}

// Fail one allocation of exactly the size of a pool block, once, in this binary only: operator new[]
// is replaced by malloc plus an armed size. Nothing else in the tests allocates an array of that size.
namespace {
std::size_t g_fail_array_size = 0;
}
void* operator new[](std::size_t n) {
  if (n == g_fail_array_size) { g_fail_array_size = 0; throw std::bad_alloc(); }
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

TEST(CellMemory, APoolBlockThatCannotBeAllocatedLeavesTheFailingCellUntouchedAndTheNextCloseRetriesIt) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    const LayeredMapParams p = base(m, true, 1);
    LayeredMap map(p);
    const std::size_t block_bytes = CoeffPool::kBlockSlots * map.coeffSlotBytes();
    const CellId first = static_cast<CellId>(CoeffPool::kBlockSlots);   // the first block holds exactly these
    const CellId n = first + 2;
    for (CellId c = 0; c < first; ++c) map.observeHit(c);
    map.endWindow();
    ASSERT_EQ(map.coeffSlotsInUse(), CoeffPool::kBlockSlots);
    for (CellId c = 0; c < n; ++c) map.observeHit(c);   // two new cells: they need a second block
    g_fail_array_size = block_bytes;
    EXPECT_THROW(map.endWindow(), std::bad_alloc);
    EXPECT_EQ(g_fail_array_size, 0u) << "the armed failure fired";
    const CellId failing = static_cast<CellId>(CoeffPool::kBlockSlots);
    EXPECT_EQ(map.coeffSlotsInUse(), CoeffPool::kBlockSlots) << "no slot was taken for the failing cell";
    EXPECT_EQ(map.observations(0), 2) << "the cells before it were finished";
    EXPECT_EQ(map.observations(failing), 0) << "the failing cell keeps its counters and evidence";
    EXPECT_EQ(map.observations(failing + 1), 0) << "and so do the cells after it";
    EXPECT_EQ(map.cellCount(), static_cast<std::size_t>(n));
    map.endWindow();   // retry
    EXPECT_EQ(map.lastWindowTouched(), 2u);
    EXPECT_EQ(map.coeffSlotsInUse(), static_cast<std::size_t>(n));
    EXPECT_EQ(map.observations(failing), 1);
    EXPECT_EQ(map.observations(failing + 1), 1);
    EXPECT_EQ(map.observations(0), 2) << "finished cells are not visited again";
  }
}
