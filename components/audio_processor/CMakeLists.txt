set(SRC_FILES "audio_processor.c")

list(APPEND SRC_FILES "audio_stream_dual_microphones.c")
list(APPEND SRC_FILES "audio_stream_mp3.c")

set(COMPONENT_SRCS ${SRC_FILES})
set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_REQUIRES audio_recorder audio_stream audio_pipeline audio_hal esp_peripherals esp-adf-libs)

register_component()

add_definitions(-DENBALE_AUDIO_STREAM_DUAL_MIC)