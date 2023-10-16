cmake_minimum_required(VERSION 3.15)
project(EIMHost VERSION 1.0.0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
add_definitions(-DJUCE_USE_MP3AUDIOFORMAT -DJUCE_PLUGINHOST_VST3 -DJUCE_PLUGINHOST_AU -DJUCE_PLUGINHOST_LADSPA)

if(MSVC)
    add_definitions(-DJUCE_PLUGINHOST_VST -DJUCE_ASIO)
endif(MSVC)

file(GLOB EIM_SRC_FILES "src/*.cpp" "src/*.h")

add_subdirectory(JavaSharedMemory)

add_subdirectory(JUCE)
juce_add_console_app(${PROJECT_NAME}
    VERSION "1.0.0"
    PRODUCT_NAME ${PROJECT_NAME}
    COMPANY_NAME "EchoInMirror"
    ICON_SMALL "resources/logo128.png"
    ICON_BIG "resources/logo512.png"
)

if(MSVC)
    _juce_configure_bundle(${PROJECT_NAME} ${PROJECT_NAME})
    _juce_add_resources_rc(${PROJECT_NAME} ${PROJECT_NAME})
endif(MSVC)

target_sources(${PROJECT_NAME} PRIVATE ${EIM_SRC_FILES})
target_compile_definitions(${PROJECT_NAME} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FREETYPE=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINERAMA=0)

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
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif(MSVC)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EIMHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EIMHost,JUCE_VERSION>")

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        java_shared_memory
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
