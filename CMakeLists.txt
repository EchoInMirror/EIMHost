cmake_minimum_required(VERSION 3.22)
project(EIMPluginHost VERSION 0.0.0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
add_definitions(-DJUCE_USE_MP3AUDIOFORMAT -DJUCE_PLUGINHOST_VST3 -DJUCE_PLUGINHOST_VST -DJUCE_PLUGINHOST_AU)

file(GLOB EIM_SRC_FILES "src/*.cpp" "src/*.h")

add_subdirectory(JUCE)
juce_add_console_app(EIMPluginHost PRODUCT_NAME "EIMPluginHost")
target_sources(EIMPluginHost PRIVATE ${EIM_SRC_FILES} ${PROTO_SRCS} ${PROTO_HDRS})

if(MSVC)
    set(vst2sdkPath ${CMAKE_CURRENT_BINARY_DIR}/vst2sdk)
    set(vst2sdkDownloadPath ${vst2sdkPath}/pluginterfaces/vst2.x)
    if(NOT EXISTS ${vst2sdkDownloadPath})
        message(STATUS "Downloading VST2SDK...")
        set(vst2sdkDownloadUrl https://raw.githubusercontent.com/juce-framework/JUCE/a54535bc317b5c005a8cda5c22a9c8d4c6e0c67e/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/vst2.x)
        file(DOWNLOAD ${vst2sdkDownloadUrl}/aeffect.h ${vst2sdkDownloadPath}/aeffect.h SHOW_PROGRESS)
        file(DOWNLOAD ${vst2sdkDownloadUrl}/aeffectx.h ${vst2sdkDownloadPath}/aeffectx.h SHOW_PROGRESS)
        file(DOWNLOAD ${vst2sdkDownloadUrl}/vstfxstore.h ${vst2sdkDownloadPath}/vstfxstore.h SHOW_PROGRESS)
        message(STATUS "Downloaded VST2SDK.")
    endif()
    include_directories(${vst2sdkPath})

    add_definitions(-D_WIN32_WINNT=0x0601)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHa")
    set_property(TARGET EIMPluginHost PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif(MSVC)

target_compile_definitions(EIMPluginHost
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EIMPluginHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EIMPluginHost,JUCE_VERSION>")

target_link_libraries(EIMPluginHost
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
