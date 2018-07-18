add_executable(tests
    common/param_package.cpp
    core/arm/arm_test_common.cpp
    core/arm/arm_test_common.h
    core/core_timing.cpp
    core/memory/memory.cpp
    glad.cpp
    tests.cpp
)

create_target_directory_groups(tests)

target_link_libraries(tests PRIVATE common core)
target_link_libraries(tests PRIVATE glad) # To support linker work-around
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} catch-single-include Threads::Threads)

add_test(NAME tests COMMAND tests)
