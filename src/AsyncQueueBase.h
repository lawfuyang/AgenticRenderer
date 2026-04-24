#pragma once

// ============================================================================
// AsyncQueueBase
// ============================================================================
// Non-template base class for single-worker background-thread queues.
//
// The queue stores type-erased std::function<void()> tasks.  The private
// background thread pops and executes tasks in FIFO order.
//
// Public API:
//   Start(logTag)      — launch background thread (call before Enqueue*).
//   Stop(logTag)       — drain queue, join thread.
//   IsRunning()        — true between Start() and Stop().
//   GetQueuedCount()   — items waiting in the queue (not yet picked up).
//   GetPendingCount()  — items queued + currently executing.
//   Flush()            — block until GetPendingCount() reaches 0.
//
// Protected API (for derived classes):
//   EnqueueTask(task)  — push a callable; increments pending count.
//
// Thread safety:
//   EnqueueTask() / GetQueuedCount() / GetPendingCount() — any thread.
//   Start() / Stop() / Flush() — single owner thread only.
// ============================================================================
class AsyncQueueBase
{
public:
    // Destructor: if the background thread was started but never stopped
    // (e.g. the owning object is destroyed without calling Stop()), join
    // it here to avoid std::terminate on a joinable thread.
    virtual ~AsyncQueueBase();

    void Start(const char* logTag);
    void Stop(const char* logTag);

    bool     IsRunning()       const { return m_bRunning; }
    uint32_t GetQueuedCount()  const;
    uint32_t GetPendingCount() const;

    // Block until all queued + in-flight tasks complete. No-op if not running.
    void Flush();

    // ── Static registry ──────────────────────────────────────────────────────
    // Queues are registered automatically on Start() and removed on Stop().
    // Access is main-thread only (Start/Stop are owner-thread; UI reads here).
    struct RegistryEntry { const char* m_Name; AsyncQueueBase* m_Queue; };
    static const std::vector<RegistryEntry>& GetActiveQueues();

private:
    inline static std::vector<RegistryEntry> ms_ActiveQueues;

protected:
    using Task = std::function<void()>;

    // Push a task onto the queue. Increments pending count and wakes the thread.
    // Thread-safe; may be called from any thread after Start().
    void EnqueueTask(Task task);

private:
    void ThreadFunc();

    mutable std::mutex      m_Mutex;
    std::condition_variable m_CV;
    std::condition_variable m_DrainCV;
    std::queue<Task>        m_Queue;
    std::thread             m_Thread;
    uint32_t                m_PendingCount = 0;
    bool                    m_bRunning     = false;
    bool                    m_bStopping    = false;
};
