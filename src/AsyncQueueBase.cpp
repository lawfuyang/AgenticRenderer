#include "pch.h"
#include "AsyncQueueBase.h"

const std::vector<AsyncQueueBase::RegistryEntry>& AsyncQueueBase::GetActiveQueues()
{
    return ms_ActiveQueues;
}

AsyncQueueBase::~AsyncQueueBase()
{
    // Safety net: if the owner forgot to call Stop() before destruction,
    // stop the thread here to avoid std::terminate on a joinable thread.
    if (m_Thread.joinable())
    {
        {
            std::lock_guard<std::mutex> lk(m_Mutex);
            m_bStopping = true;
        }
        m_CV.notify_one();
        m_Thread.join();
        m_bRunning = false;

        ms_ActiveQueues.erase(
            std::remove_if(ms_ActiveQueues.begin(), ms_ActiveQueues.end(),
                [this](const RegistryEntry& e) { return e.m_Queue == this; }),
            ms_ActiveQueues.end());
    }
}

void AsyncQueueBase::Start(const char* logTag)
{
    SDL_assert(!m_bRunning && "AsyncQueueBase::Start() called while already running");
    {
        std::lock_guard<std::mutex> lk(m_Mutex);
        m_bStopping = false;
        // Do NOT reset m_PendingCount here: items may have been enqueued before
        // Start() was called, and after a proper Stop() the count is already 0.
    }
    m_bRunning = true;
    m_Thread   = std::thread(&AsyncQueueBase::ThreadFunc, this);
    ms_ActiveQueues.push_back({ logTag, this });
    SDL_Log("[%s] Background thread started", logTag);
}

void AsyncQueueBase::Stop(const char* logTag)
{
    if (!m_Thread.joinable())
    {
        m_bRunning = false;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_Mutex);
        m_bStopping = true;
    }
    m_CV.notify_one();
    m_Thread.join();
    m_bRunning = false;

    ms_ActiveQueues.erase(
        std::remove_if(ms_ActiveQueues.begin(), ms_ActiveQueues.end(),
            [this](const RegistryEntry& e) { return e.m_Queue == this; }),
        ms_ActiveQueues.end());

    SDL_Log("[%s] Background thread stopped", logTag);
}

uint32_t AsyncQueueBase::GetQueuedCount() const
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    return static_cast<uint32_t>(m_Queue.size());
}

uint32_t AsyncQueueBase::GetPendingCount() const
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    return m_PendingCount;
}

void AsyncQueueBase::Flush()
{
    if (!m_bRunning)
        return;
    std::unique_lock<std::mutex> lk(m_Mutex);
    m_DrainCV.wait(lk, [this]() { return m_PendingCount == 0; });
}

void AsyncQueueBase::EnqueueTask(Task task)
{
    {
        std::lock_guard<std::mutex> lk(m_Mutex);
        ++m_PendingCount;
        m_Queue.push(std::move(task));
    }
    m_CV.notify_one();
}

void AsyncQueueBase::ThreadFunc()
{
    while (true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_CV.wait(lock, [this]() { return !m_Queue.empty() || m_bStopping; });

            // Drain the queue completely before honouring a stop request.
            if (m_bStopping && m_Queue.empty())
                break;

            task = std::move(m_Queue.front());
            m_Queue.pop();
        }

        task();

        {
            std::lock_guard<std::mutex> lk(m_Mutex);
            SDL_assert(m_PendingCount > 0 && "AsyncQueueBase: pending count underflow");
            if (--m_PendingCount == 0)
                m_DrainCV.notify_all();
        }
    }
}
