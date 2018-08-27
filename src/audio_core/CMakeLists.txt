add_library(audio_core STATIC
    algorithm/filter.cpp
    algorithm/filter.h
    algorithm/interpolate.cpp
    algorithm/interpolate.h
    audio_out.cpp
    audio_out.h
    audio_renderer.cpp
    audio_renderer.h
    buffer.h
    codec.cpp
    codec.h
    null_sink.h
    sink.h
    sink_details.cpp
    sink_details.h
    sink_stream.h
    stream.cpp
    stream.h
    time_stretch.cpp
    time_stretch.h

    $<$<BOOL:${ENABLE_CUBEB}>:cubeb_sink.cpp cubeb_sink.h>
)

create_target_directory_groups(audio_core)

target_link_libraries(audio_core PUBLIC common core)
target_link_libraries(audio_core PRIVATE SoundTouch)

if(ENABLE_CUBEB)
    target_link_libraries(audio_core PRIVATE cubeb)
    target_compile_definitions(audio_core PRIVATE -DHAVE_CUBEB=1)
endif()
