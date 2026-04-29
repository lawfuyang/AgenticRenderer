// Tests_CoreBoot.cpp - Core Boot Tests
//
// Systems under test: TaskScheduler, Config, Utilities (timer, math)
// Setup required: None (CPU-only, no GPU/RHI)
//
// Run with: HobbyRenderer --run-tests=*CoreBoot*
// ============================================================================

// TestFixtures.h includes doctest.h (without DOCTEST_CONFIG_IMPLEMENT) and
// all shared headers. DOCTEST_CONFIG_IMPLEMENT lives in TestMain.cpp.
#include "TestFixtures.h"

// ============================================================================
// TEST SUITE: TaskScheduler
// ============================================================================
TEST_SUITE("TaskScheduler")
{
    // ------------------------------------------------------------------
    // TC-TS-01: Default construction creates kRuntimeThreadCount workers
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-01 Thread pool creation - default thread count")
    {
        TaskScheduler scheduler;
        CHECK(scheduler.GetThreadCount() == TaskScheduler::kRuntimeThreadCount);
    }

    // ------------------------------------------------------------------
    // TC-TS-02: SetThreadCount reduces the pool
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-02 Thread pool resize - reduce thread count")
    {
        TaskScheduler scheduler;
        scheduler.SetThreadCount(4);
        CHECK(scheduler.GetThreadCount() == 4);
    }

    // ------------------------------------------------------------------
    // TC-TS-03: SetThreadCount increases the pool
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-03 Thread pool resize - increase thread count")
    {
        TaskScheduler scheduler;
        scheduler.SetThreadCount(2);
        scheduler.SetThreadCount(8);
        CHECK(scheduler.GetThreadCount() == 8);
    }

    // ------------------------------------------------------------------
    // TC-TS-04: ParallelFor executes all items exactly once
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-04 ParallelFor - all items executed exactly once")
    {
        TaskScheduler scheduler;
        constexpr uint32_t kCount = 1000;
        std::vector<std::atomic<int>> counters(kCount);
        for (auto& c : counters) c.store(0);

        scheduler.ParallelFor(kCount, [&](uint32_t index, uint32_t /*threadIndex*/)
        {
            counters[index].fetch_add(1);
        });

        for (uint32_t i = 0; i < kCount; ++i)
        {
            CHECK(counters[i].load() == 1);
        }
    }

    // ------------------------------------------------------------------
    // TC-TS-05: ParallelFor with count=0 is a no-op
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-05 ParallelFor - zero count is a no-op")
    {
        TaskScheduler scheduler;
        bool called = false;
        scheduler.ParallelFor(0, [&](uint32_t, uint32_t)
        {
            called = true;
        });
        CHECK_FALSE(called);
    }

    // ------------------------------------------------------------------
    // TC-TS-06: ScheduleTask (immediate) executes the task
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-06 ScheduleTask - immediate execution")
    {
        TaskScheduler scheduler;
        std::atomic<int> counter{ 0 };

        scheduler.ScheduleTask([&counter]()
        {
            counter.fetch_add(1);
        }, /*bImmediateExecute=*/true);

        // Wait for the task to complete
        scheduler.ExecuteAllScheduledTasks();

        CHECK(counter.load() == 1);
    }

    // ------------------------------------------------------------------
    // TC-TS-07: ScheduleTask (deferred) executes on ExecuteAllScheduledTasks
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-07 ScheduleTask - deferred execution via ExecuteAllScheduledTasks")
    {
        TaskScheduler scheduler;
        std::atomic<int> counter{ 0 };

        scheduler.ScheduleTask([&counter]()
        {
            counter.fetch_add(1);
        }, /*bImmediateExecute=*/false);

        // Should not have run yet
        CHECK(counter.load() == 0);

        scheduler.ExecuteAllScheduledTasks();

        CHECK(counter.load() == 1);
    }

    // ------------------------------------------------------------------
    // TC-TS-08: Multiple deferred tasks all execute
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-08 ScheduleTask - multiple deferred tasks all execute")
    {
        TaskScheduler scheduler;
        constexpr int kTaskCount = 50;
        std::atomic<int> counter{ 0 };

        for (int i = 0; i < kTaskCount; ++i)
        {
            scheduler.ScheduleTask([&counter]()
            {
                counter.fetch_add(1);
            }, /*bImmediateExecute=*/false);
        }

        scheduler.ExecuteAllScheduledTasks();

        CHECK(counter.load() == kTaskCount);
    }

    // ------------------------------------------------------------------
    // TC-TS-09: ParallelFor accumulates a sum correctly (data race check)
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-09 ParallelFor - atomic accumulation is correct")
    {
        TaskScheduler scheduler;
        constexpr uint32_t kCount = 500;
        std::atomic<uint64_t> sum{ 0 };

        scheduler.ParallelFor(kCount, [&](uint32_t index, uint32_t)
        {
            sum.fetch_add(index);
        });

        // Expected: 0 + 1 + ... + (kCount-1) = kCount*(kCount-1)/2
        const uint64_t expected = static_cast<uint64_t>(kCount) * (kCount - 1) / 2;
        CHECK(sum.load() == expected);
    }

    // ------------------------------------------------------------------
    // TC-TS-10: Thread indices are within valid range
    // ------------------------------------------------------------------
    TEST_CASE("TC-TS-10 ParallelFor - thread indices are within valid range")
    {
        TaskScheduler scheduler;
        const uint32_t threadCount = scheduler.GetThreadCount();
        std::atomic<bool> outOfRange{ false };

        scheduler.ParallelFor(200, [&](uint32_t, uint32_t threadIndex)
        {
            // threadIndex can be 0..threadCount (main thread uses threadCount as its index)
            if (threadIndex > threadCount)
                outOfRange.store(true);
        });

        CHECK_FALSE(outOfRange.load());
    }
}

// ============================================================================
// TEST SUITE: Config
// ============================================================================
TEST_SUITE("Config")
{
    // ------------------------------------------------------------------
    // TC-CFG-06: ParseCommandLine sets --rhidebug
    // ------------------------------------------------------------------
    TEST_CASE("TC-CFG-06 ParseCommandLine - --rhidebug enables validation")
    {
        ConfigGuard guard; // restore on exit

        const char* argv[] = { "HobbyRenderer", "--rhidebug" };
        Config::ParseCommandLine(2, const_cast<char**>(argv));

        CHECK(Config::Get().m_EnableValidation);
    }

    // ------------------------------------------------------------------
    // TC-CFG-09: ParseCommandLine sets --disable-rendergraph-aliasing
    // ------------------------------------------------------------------
    TEST_CASE("TC-CFG-09 ParseCommandLine - --disable-rendergraph-aliasing clears flag")
    {
        ConfigGuard guard;

        const char* argv[] = { "HobbyRenderer", "--disable-rendergraph-aliasing" };
        Config::ParseCommandLine(2, const_cast<char**>(argv));

        CHECK_FALSE(Config::Get().m_EnableRenderGraphAliasing);
    }

    // ------------------------------------------------------------------
    // TC-CFG-10: ParseCommandLine sets --execute-per-pass
    // ------------------------------------------------------------------
    TEST_CASE("TC-CFG-10 ParseCommandLine - --execute-per-pass sets flag")
    {
        ConfigGuard guard;

        const char* argv[] = { "HobbyRenderer", "--execute-per-pass" };
        Config::ParseCommandLine(2, const_cast<char**>(argv));

        CHECK(Config::Get().ExecutePerPass);
    }

    // ------------------------------------------------------------------
    // TC-CFG-11: ParseCommandLine sets --execute-per-pass-and-wait
    // ------------------------------------------------------------------
    TEST_CASE("TC-CFG-11 ParseCommandLine - --execute-per-pass-and-wait sets flag")
    {
        ConfigGuard guard;

        const char* argv[] = { "HobbyRenderer", "--execute-per-pass-and-wait" };
        Config::ParseCommandLine(2, const_cast<char**>(argv));

        CHECK(Config::Get().ExecutePerPassAndWait);
    }

    // ------------------------------------------------------------------
    // TC-CFG-12: Unknown arguments are silently ignored (no crash)
    // ------------------------------------------------------------------
    TEST_CASE("TC-CFG-12 ParseCommandLine - unknown arguments do not crash")
    {
        ConfigGuard guard;

        const char* argv[] = { "HobbyRenderer", "--totally-unknown-flag-xyz" };
        // Should not throw or assert
        Config::ParseCommandLine(2, const_cast<char**>(argv));
        CHECK(true); // reached here = no crash
    }
}

// ============================================================================
// TEST SUITE: Timer
// ============================================================================
TEST_SUITE("Timer")
{
    // ------------------------------------------------------------------
    // TC-TMR-02: SimpleTimer measures elapsed time with reasonable accuracy
    //            We sleep 50 ms and expect 40–200 ms (generous bounds for CI).
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMR-02 SimpleTimer - measures elapsed time accurately")
    {
        SimpleTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double elapsed = t.TotalMilliseconds();

        // Allow generous bounds: 40 ms – 500 ms
        CHECK(elapsed >= 40.0);
        CHECK(elapsed <= 500.0);
    }

    // ------------------------------------------------------------------
    // TC-TMR-03: SimpleTimer Reset restarts the clock
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMR-03 SimpleTimer - Reset restarts elapsed time")
    {
        SimpleTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        t.Reset();
        const double afterReset = t.TotalMilliseconds();

        // After reset the elapsed should be very small (< 30 ms)
        CHECK(afterReset < 30.0);
    }

    // ------------------------------------------------------------------
    // TC-TMR-05: SimpleTimer LapSeconds resets the lap counter
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMR-05 SimpleTimer - consecutive LapSeconds are independent")
    {
        SimpleTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const double lap1 = t.LapSeconds();

        // Immediately take another lap - should be very small
        const double lap2 = t.LapSeconds();

        CHECK(lap1 >= 0.0);
        // lap2 should be much smaller than lap1 (< 10 ms)
        CHECK(lap2 < 0.010);
    }

    // ------------------------------------------------------------------
    // TC-TMR-06: SecondsToMilliseconds conversion is correct
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMR-06 SimpleTimer - SecondsToMilliseconds conversion")
    {
        CHECK(SimpleTimer::SecondsToMilliseconds(1.0f) == doctest::Approx(1000.0f));
        CHECK(SimpleTimer::SecondsToMilliseconds(0.5f) == doctest::Approx(500.0f));
        CHECK(SimpleTimer::SecondsToMilliseconds(0.0f) == doctest::Approx(0.0f));
    }
}

// ============================================================================
// TEST SUITE: Math
// ============================================================================
TEST_SUITE("Math")
{
    // ------------------------------------------------------------------
    // TC-MATH-01: NextLowerPow2 - basic cases
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-01 NextLowerPow2 - basic cases")
    {
        CHECK(NextLowerPow2(1u)   == 1u);
        CHECK(NextLowerPow2(2u)   == 2u);
        CHECK(NextLowerPow2(3u)   == 2u);
        CHECK(NextLowerPow2(4u)   == 4u);
        CHECK(NextLowerPow2(5u)   == 4u);
        CHECK(NextLowerPow2(7u)   == 4u);
        CHECK(NextLowerPow2(8u)   == 8u);
        CHECK(NextLowerPow2(255u) == 128u);
        CHECK(NextLowerPow2(256u) == 256u);
        CHECK(NextLowerPow2(1024u) == 1024u);
        CHECK(NextLowerPow2(1025u) == 1024u);
    }

    // ------------------------------------------------------------------
    // TC-MATH-02: NextPow2 - basic cases
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-02 NextPow2 - basic cases")
    {
        CHECK(NextPow2(1)   == 1);
        CHECK(NextPow2(2)   == 2);
        CHECK(NextPow2(3)   == 4);
        CHECK(NextPow2(4)   == 4);
        CHECK(NextPow2(5)   == 8);
        CHECK(NextPow2(7)   == 8);
        CHECK(NextPow2(8)   == 8);
        CHECK(NextPow2(9)   == 16);
        CHECK(NextPow2(255) == 256);
        CHECK(NextPow2(256) == 256);
        CHECK(NextPow2(257) == 512);
        CHECK(NextPow2(1000) == 1024);
    }

    // ------------------------------------------------------------------
    // TC-MATH-03: DivideAndRoundUp - basic cases
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-03 DivideAndRoundUp - basic cases")
    {
        CHECK(DivideAndRoundUp(0u, 4u)  == 0u);
        CHECK(DivideAndRoundUp(1u, 4u)  == 1u);
        CHECK(DivideAndRoundUp(4u, 4u)  == 1u);
        CHECK(DivideAndRoundUp(5u, 4u)  == 2u);
        CHECK(DivideAndRoundUp(8u, 4u)  == 2u);
        CHECK(DivideAndRoundUp(9u, 4u)  == 3u);
        CHECK(DivideAndRoundUp(100u, 7u) == 15u);  // 100/7 = 14.28... → 15
        CHECK(DivideAndRoundUp(7u, 7u)  == 1u);
        CHECK(DivideAndRoundUp(1u, 1u)  == 1u);
    }

    // ------------------------------------------------------------------
    // TC-MATH-04: DivideAndRoundUp - exact multiples
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-04 DivideAndRoundUp - exact multiples produce no rounding")
    {
        for (uint32_t d = 1; d <= 16; ++d)
        {
            for (uint32_t n = 0; n <= 64; n += d)
            {
                CHECK(DivideAndRoundUp(n, d) == n / d);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-MATH-05: Halton sequence - base 2 first few values
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-05 Halton - base-2 sequence values")
    {
        // Known Halton base-2: 1/2, 1/4, 3/4, 1/8, 5/8, 3/8, 7/8, ...
        CHECK(Halton(1, 2) == doctest::Approx(0.5f));
        CHECK(Halton(2, 2) == doctest::Approx(0.25f));
        CHECK(Halton(3, 2) == doctest::Approx(0.75f));
        CHECK(Halton(4, 2) == doctest::Approx(0.125f));
        CHECK(Halton(5, 2) == doctest::Approx(0.625f));
    }

    // ------------------------------------------------------------------
    // TC-MATH-06: Halton sequence - base 3 first few values
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-06 Halton - base-3 sequence values")
    {
        // Known Halton base-3: 1/3, 2/3, 1/9, 4/9, 7/9, ...
        CHECK(Halton(1, 3) == doctest::Approx(1.0f / 3.0f));
        CHECK(Halton(2, 3) == doctest::Approx(2.0f / 3.0f));
        CHECK(Halton(3, 3) == doctest::Approx(1.0f / 9.0f));
        CHECK(Halton(4, 3) == doctest::Approx(4.0f / 9.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATH-07: Halton values are in [0, 1)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-07 Halton - all values are in [0, 1)")
    {
        for (uint32_t i = 1; i <= 64; ++i)
        {
            const float v2 = Halton(i, 2);
            const float v3 = Halton(i, 3);
            CHECK(v2 >= 0.0f);
            CHECK(v2 < 1.0f);
            CHECK(v3 >= 0.0f);
            CHECK(v3 < 1.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-MATH-16: CalculateGridZParams returns finite Vector3
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-16 CalculateGridZParams - returns finite components")
    {
        const Vector3 result = CalculateGridZParams(0.1f, 1000.0f, 1.0f, 32);
        CHECK(std::isfinite(result.x));
        CHECK(std::isfinite(result.y));
        CHECK(std::isfinite(result.z));
    }

    // ------------------------------------------------------------------
    // TC-MATH-17: CalculateGridZParams x component is positive
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-17 CalculateGridZParams - x component is positive")
    {
        const Vector3 result = CalculateGridZParams(0.1f, 1000.0f, 1.0f, 32);
        CHECK(result.x > 0.0f);
    }

    // ------------------------------------------------------------------
    // TC-MATH-18: CreateInvDeviceZToWorldZTransform returns finite Vector2
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-18 CreateInvDeviceZToWorldZTransform - returns finite Vector2")
    {
        using namespace DirectX;
        // Use a standard reverse-Z perspective matrix
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, 16.0f / 9.0f, 100.0f, 0.1f);
        Matrix mat;
        XMStoreFloat4x4(&mat, proj);
        const Vector2 result = CreateInvDeviceZToWorldZTransform(mat);
        CHECK(std::isfinite(result.x));
        CHECK(std::isfinite(result.y));
    }

    // ------------------------------------------------------------------
    // TC-MATH-20: HashToUint different inputs generally produce different outputs
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-20 HashToUint - different hashes produce different uint values")
    {
        const size_t hashA = std::hash<std::string>{}("alpha");
        const size_t hashB = std::hash<std::string>{}("beta");
        // This is a probabilistic test — hash collisions are theoretically possible.
        // With distinct string hashes this should always differ.
        const uint32_t ra = HashToUint(hashA);
        const uint32_t rb = HashToUint(hashB);
        if (hashA != hashB)
            CHECK(ra != rb);
    }

    // ------------------------------------------------------------------
    // TC-MATH-22: DivideAndRoundUp with numerator just below divisor
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-22 DivideAndRoundUp - numerator < divisor gives 1")
    {
        CHECK(DivideAndRoundUp(1u, 128u) == 1u);
        CHECK(DivideAndRoundUp(127u, 128u) == 1u);
    }

    // ------------------------------------------------------------------
    // TC-MATH-23: DivideAndRoundUp with UINT32_MAX-safe values
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-23 DivideAndRoundUp - large but safe values")
    {
        // 0x7FFF0000 / 65536 = 32767 exactly (no overflow in numerator+divisor-1)
        constexpr uint32_t n = 0x7FFF0000u;
        constexpr uint32_t d = 65536u;
        CHECK(DivideAndRoundUp(n, d) == n / d);
    }

    // ------------------------------------------------------------------
    // TC-MATH-24: Halton base-5 first values
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATH-24 Halton - base-5 first values")
    {
        // Base-5: 1/5, 2/5, 3/5, 4/5, 1/25, ...
        CHECK(Halton(1, 5) == doctest::Approx(0.2f));
        CHECK(Halton(2, 5) == doctest::Approx(0.4f));
        CHECK(Halton(3, 5) == doctest::Approx(0.6f));
        CHECK(Halton(4, 5) == doctest::Approx(0.8f));
    }

}

// ============================================================================
// TEST SUITE: TaskScheduler_Extended
// ============================================================================
TEST_SUITE("TaskScheduler_Extended")
{
    // ------------------------------------------------------------------
    // TC-TSX-01: SetThreadCount(1) single thread still processes all items
    // ------------------------------------------------------------------
    TEST_CASE("TC-TSX-01 SingleThread - ParallelFor with 1 thread executes all items")
    {
        TaskScheduler scheduler;
        scheduler.SetThreadCount(1);
        REQUIRE(scheduler.GetThreadCount() == 1);

        std::vector<std::atomic<int>> counters(50);
        for (auto& c : counters) c.store(0);

        scheduler.ParallelFor(50, [&](uint32_t index, uint32_t)
        {
            counters[index].fetch_add(1);
        });

        for (int i = 0; i < 50; ++i)
            CHECK(counters[i].load() == 1);
    }

    // ------------------------------------------------------------------
    // TC-TSX-05: SetThreadCount preserves pending tasks
    // ------------------------------------------------------------------
    TEST_CASE("TC-TSX-05 ThreadCount - tasks scheduled before resize still execute")
    {
        TaskScheduler scheduler;
        std::atomic<int> counter{ 0 };

        scheduler.ScheduleTask([&counter]() { counter.fetch_add(1); }, false);
        scheduler.SetThreadCount(2); // resize with a pending task

        scheduler.ExecuteAllScheduledTasks();
        CHECK(counter.load() == 1);
    }

    // ------------------------------------------------------------------
    // TC-TSX-06: ParallelFor with maximum thread count still completes
    // ------------------------------------------------------------------
    TEST_CASE("TC-TSX-06 MaxThreads - ParallelFor with max threads completes")
    {
        TaskScheduler scheduler;
        // Set a larger thread count (capped internally to hw_concurrency)
        scheduler.SetThreadCount(64);

        std::atomic<int> counter{ 0 };
        scheduler.ParallelFor(200, [&](uint32_t, uint32_t)
        {
            counter.fetch_add(1);
        });

        CHECK(counter.load() == 200);
    }
}

// ============================================================================
// TEST SUITE: Config_Extended
// ============================================================================
TEST_SUITE("Config_Extended")
{
    // ------------------------------------------------------------------
    // TC-CFGX-01: ParseCommandLine with argc=1 (program name only) doesn't crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-CFGX-01 ParseCommandLine - argc=1 does not crash")
    {
        ConfigGuard guard;
        const char* argv[] = { "HobbyRenderer" };
        CHECK_NOTHROW(Config::ParseCommandLine(1, const_cast<char**>(argv)));
    }

}

// ============================================================================
// TEST SUITE: Timer_Extended
// ============================================================================
TEST_SUITE("Timer_Extended")
{
    // ------------------------------------------------------------------
    // TC-TMRX-01: Two simultaneous timers run independently
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMRX-01 SimultaneousTimers - two timers are independent")
    {
        SimpleTimer t1, t2;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        const double e1 = t1.TotalMilliseconds();
        const double e2 = t2.TotalMilliseconds();

        // Both timers started at nearly the same time, so they should be
        // within 5 ms of each other.
        CHECK(std::abs(e1 - e2) < 5.0);
    }

    // ------------------------------------------------------------------
    // TC-TMRX-02: TotalMilliseconds = TotalSeconds * 1000 (consistency)
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMRX-02 TimerConsistency - TotalMilliseconds == TotalSeconds * 1000")
    {
        SimpleTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        const double sec = t.TotalSeconds();
        const double ms  = t.TotalMilliseconds();

        // Allow 1 ms tolerance for the two reads not being simultaneous.
        CHECK(std::abs(ms - sec * 1000.0) < 1.0);
    }

    // ------------------------------------------------------------------
    // TC-TMRX-04: ScopedTimerLog construction and destruction do not crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMRX-04 ScopedTimerLog - ctor/dtor do not crash")
    {
        CHECK_NOTHROW({
            ScopedTimerLog stl("TC-TMRX-04");
            (void)stl;
        });
    }

    // ------------------------------------------------------------------
    // TC-TMRX-05: SingleThreadGuard construction and destruction do not crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-TMRX-05 SingleThreadGuard - ctor/dtor do not crash")
    {
        std::atomic<int> count = 0;
        CHECK_NOTHROW({
            SingleThreadGuard g(count);
            CHECK(count.load() == 1);
        });
        CHECK(count.load() == 0);
    }
}
