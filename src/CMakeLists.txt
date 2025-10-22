if (CONFIG_SPN_CORE)
    add_subdirectory(core)
endif ()

if (CONFIG_SPN_EXAMPLE_LIB)
    add_subdirectory(example_lib)
endif ()

if (CONFIG_SPN_LOGGING)
    add_subdirectory(logging)
endif ()

if (CONFIG_SPN_DEBUGGING)
    add_subdirectory(debugging)
endif ()

if (CONFIG_SPN_SYSTEM)
    add_subdirectory(system)
endif ()

if (CONFIG_SPN_THREADING)
    add_subdirectory(threading)
endif ()

if (CONFIG_SPN_ALLOCATION)
    add_subdirectory(allocation)
endif ()

if (CONFIG_SPN_CONTAINERS)
    add_subdirectory(containers)
endif ()

if (CONFIG_SPN_DEPENDENCY_INJECTION)
    add_subdirectory(dependency_injection)
endif ()

if (CONFIG_SPN_TIMING)
    add_subdirectory(timing)
endif ()

