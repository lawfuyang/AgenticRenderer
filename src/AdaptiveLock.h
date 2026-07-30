#pragma once

// AdaptiveLock: starts as a pure spinlock.
// After a configurable number of failed spins it "converts" itself to a
// std::mutex so that waiting threads block instead of burning CPU.
//
// Memory-order discipline is deliberately the absolute minimum that is still
// correct:
//   - acquire on every successful lock / on observing the "converted" flag
//   - release on unlock
//   - relaxed everywhere else (counters, the conversion flag itself while it
//     is still false, the spin-loop test, etc.)
//
// The conversion is one-way and irreversible; once the mutex path is taken
// the lock stays on that path forever.

class AdaptiveLock {
public:
    // How many times a thread may fail to acquire before it forces conversion.
    // Tunable; 1000–4000 is a reasonable range on modern hardware.
    static constexpr uint32_t kSpinThreshold = 2000;

    // non-copyable / non-movable
    AdaptiveLock(const AdaptiveLock&) = delete;
    AdaptiveLock& operator=(const AdaptiveLock&) = delete;

    void lock() noexcept
    {
        // Fast path – pure spin while the lock is still in spin mode.
        if (!m_bConverted.load(std::memory_order_relaxed))
        {
            for (uint32_t i = 0; i < kSpinThreshold; ++i)
            {
                // Try to take the lock with a single CAS.
                // Success → acquire semantics so everything after the lock
                // is ordered w.r.t. the previous unlock.
                State expected = State::Free;
                if (m_State.compare_exchange_weak(
                        expected, State::Locked,
                        std::memory_order_acquire,   // success
                        std::memory_order_relaxed))  // failure
                {
                    return; // acquired while still spinning
                }

                // Pause / yield to reduce contention on the cache line.
                // (On x86 this is just a PAUSE instruction.)
                std::this_thread::yield();
            }

            // Spin limit reached → convert the lock to a mutex.
            ConvertToMutex();
        }

        // Slow path – we are (or just became) a normal mutex.
        // The acquire load of m_bConverted above (or the one inside
        // ConvertToMutex) guarantees that we see the mutex fully
        // constructed.
        m_Mutex.lock();
    }

    bool try_lock() noexcept
    {
        if (!m_bConverted.load(std::memory_order_relaxed))
        {
            State expected = State::Free;
            return m_State.compare_exchange_strong(
                expected, State::Locked,
                std::memory_order_acquire,
                std::memory_order_relaxed);
        }
        return m_Mutex.try_lock();
    }

    void unlock() noexcept
    {
        if (!m_bConverted.load(std::memory_order_relaxed))
        {
            // Still in spin mode – release the flag.
            // release ordering pairs with the acquire of the successful CAS.
            m_State.store(State::Free, std::memory_order_release);
            return;
        }
        m_Mutex.unlock();
    }

private:
    enum class State : uint8_t { Free = 0, Locked = 1 };

    // The spin-lock state (only used while m_bConverted == false).
    std::atomic<State> m_State = State::Free;

    // Diagnostic / statistics – never used for synchronisation.
    std::atomic<uint32_t> m_Spins = 0;

    // One-way conversion flag.
    // Written with release, read with acquire (or relaxed when we already
    // know the conversion has happened).
    std::atomic<bool> m_bConverted = false;

    // The real mutex that takes over after conversion.
    // Constructed lazily so that the AdaptiveLock object stays tiny
    // until conversion actually occurs.
    std::mutex m_Mutex;

    // Convert the lock from pure-spin to mutex-protected.
    // Only one thread will succeed in the CAS; the others will simply
    // observe that conversion has already happened.
    void ConvertToMutex() noexcept {
        // First thread to reach here does the real conversion.
        bool bExpected = false;
        if (m_bConverted.compare_exchange_strong(
                bExpected, true,
                std::memory_order_acq_rel,   // success – publish the mutex
                std::memory_order_acquire))  // failure – just observe
        {
            // We won the race.  The mutex is already default-constructed,
            // so nothing else needs to be done.  Any thread that later
            // sees m_bConverted == true is guaranteed (by the acq_rel) to
            // see a fully-constructed m_Mutex.
        }
        // else: another thread already converted; the acquire on the
        // failed CAS is enough to synchronise with that thread.
    }
};
