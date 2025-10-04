#pragma once

#include "spn/threading/mutex.hpp"

namespace spn::queue::detail {

struct NullGuard {
    NullGuard() = default;
};

/// Multi-producer multi-consumer policy: shared mutex serializes all accesses.
class MPMCLockPolicy {
public:
    auto producer_lock() { return _mutex.lockguard(); }
    auto consumer_lock() { return _mutex.lockguard(); }
    auto clear_lock() { return _mutex.lockguard(); }

private:
    Mutex _mutex;
};

/// Multi-producer single-consumer policy: producers share a mutex, consumer is single-threaded.
class MPSCLockPolicy {
public:
    auto producer_lock() { return _producer_mutex.lockguard(); }
    auto consumer_lock() { return NullGuard{}; }
    auto clear_lock() { return _producer_mutex.lockguard(); }

private:
    Mutex _producer_mutex;
};

/// Single-producer multi-consumer policy: consumers share a mutex, producer has exclusive ownership.
class SPMCLockPolicy {
public:
    auto producer_lock() { return NullGuard{}; }
    auto consumer_lock() { return _consumer_mutex.lockguard(); }
    auto clear_lock() { return _consumer_mutex.lockguard(); }

private:
    Mutex _consumer_mutex;
};

} // namespace spn::queue::detail
