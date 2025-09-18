#include <zephyr/kernel.h>

// allocation
#include "spn/allocation/heap.hpp"
#include "spn/allocation/pool.hpp"
#include "spn/allocation/shared_ptr.hpp"
#include "spn/allocation/slab.hpp"
#include "spn/allocation/unique_ptr.hpp"

// containers
#include "spn/containers/callback.hpp"
#include "spn/containers/mpmc_deque.hpp"
#include "spn/containers/mpmc_queue.hpp"
#include "spn/containers/mpsc_queue.hpp"
#include "spn/containers/poll.hpp"

// core
#include "spn/core/enum_flags.hpp"
#include "spn/core/tuple_utils.hpp"
#include "spn/core/type_traits.hpp"
#include "spn/core/types.hpp"

// debugging
#include "spn/debugging/assert.hpp"

// dependency_injection
#include "spn/dependency_injection/container.hpp"
#include "spn/dependency_injection/phases.hpp"
#include "spn/dependency_injection/providers.hpp"

// example_lib
#include "spn/example_lib/example_lib.hpp"

// logging
#include "spn/logging/logging.hpp"
#include "spn/logging/memory.hpp"

// threading
#include "spn/threading/condition.hpp"
#include "spn/threading/event.hpp"
#include "spn/threading/lockguard.hpp"
#include "spn/threading/mutex.hpp"
#include "spn/threading/semaphore.hpp"
#include "spn/threading/thread.hpp"
#include "spn/threading/work.hpp"

// note: this is a pure stub for ide's like clion which only lint/analyze files which are part of a target.

int main(void) { return 0; }
