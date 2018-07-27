add_library(audio_core STATIC
    audio_out.cpp
    audio_out.h
    buffer.h
    stream.cpp
    stream.h
)

create_target_directory_groups(audio_core)

target_link_libraries(audio_core PUBLIC common core)
