if(CONFIG_SPN_CORE)
    add_subdirectory(core)
endif()

if(CONFIG_SPN_EXAMPLE_LIB)
    add_subdirectory(example_lib)
endif()

if(CONFIG_SPN_LOGGING)
    add_subdirectory(logging)
endif()

if(CONFIG_SPN_DEBUGGING)
    add_subdirectory(debugging)
endif()
