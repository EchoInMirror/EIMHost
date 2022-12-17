cmake_minimum_required(VERSION 3.22)
project(EIMHost VERSION 0.0.0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
add_definitions(-DJUCE_USE_MP3AUDIOFORMAT -DJUCE_PLUGINHOST_VST3 -DJUCE_PLUGINHOST_VST -DJUCE_PLUGINHOST_AU -DJUCE_ASIO)

file(GLOB EIM_SRC_FILES "src/*.cpp" "src/*.h")

add_subdirectory(JUCE)
juce_add_console_app(EIMHost
    VERSION "0.0.0"
    PRODUCT_NAME "EIMHost"
    COMPANY_NAME "Echo In Mirror"
    ICON_SMALL "resources/logo128.png"
    ICON_BIG "resources/logo512.png"
)
_juce_configure_bundle(EIMHost EIMHost)
_juce_add_resources_rc(EIMHost EIMHost)
target_sources(EIMHost PRIVATE ${EIM_SRC_FILES})

if(MSVC)
    set(thirdpartyPath ${CMAKE_CURRENT_BINARY_DIR}/thirdparty)
    set(vst2sdkDownloadPath ${thirdpartyPath}/pluginterfaces/vst2.x)
    if(NOT EXISTS ${vst2sdkDownloadPath})
        message(STATUS "Downloading VST2SDK...")
        set(vst2sdkDownloadUrl https://raw.githubusercontent.com/juce-framework/JUCE/a54535bc317b5c005a8cda5c22a9c8d4c6e0c67e/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/vst2.x)
        file(DOWNLOAD ${vst2sdkDownloadUrl}/aeffect.h ${vst2sdkDownloadPath}/aeffect.h SHOW_PROGRESS)
        file(DOWNLOAD ${vst2sdkDownloadUrl}/aeffectx.h ${vst2sdkDownloadPath}/aeffectx.h SHOW_PROGRESS)
        file(DOWNLOAD ${vst2sdkDownloadUrl}/vstfxstore.h ${vst2sdkDownloadPath}/vstfxstore.h SHOW_PROGRESS)
        message(STATUS "Downloaded VST2SDK.")
    endif()

    set(asioSdkDownloadPath ${CMAKE_CURRENT_BINARY_DIR}/AsioSDK.zip)
    if(NOT EXISTS ${asioSdkDownloadPath})
        message(STATUS "Downloading ASIO_SDK...")
        file(DOWNLOAD https://www.steinberg.net/asiosdk ${asioSdkDownloadPath})
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xzf ${asioSdkDownloadPath} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        file(GLOB asioHeaders ${CMAKE_CURRENT_BINARY_DIR}/asiosdk_2.3.3_2019-06-14/common/*.h)
        foreach(asioHeaderFile ${asioHeaders})
            get_filename_component(asioFileName ${asioHeaderFile} NAME)
            configure_file(${asioHeaderFile} ${thirdpartyPath}/${asioFileName} COPYONLY)
        endforeach()
        message(STATUS "Downloaded ASIO_SDK.")
    endif()

    include_directories(${thirdpartyPath})

    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHa")
    set_property(TARGET EIMHost PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif(MSVC)

target_compile_definitions(EIMHost
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EIMHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EIMHost,JUCE_VERSION>")

target_link_libraries(EIMHost
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
