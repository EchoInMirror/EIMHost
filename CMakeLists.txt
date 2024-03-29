cmake_minimum_required(VERSION 3.15)
project(EIMHost VERSION 1.0.0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
add_definitions(-DJUCE_USE_MP3AUDIOFORMAT -DJUCE_PLUGINHOST_VST3 -DJUCE_PLUGINHOST_AU -DJUCE_PLUGINHOST_LADSPA -DJUCE_PLUGINHOST_LV2 -DJUCE_PLUGINHOST_ARA -DVST_LOGGING=0)

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffast-math -ftree-vectorize")
endif()

if(WIN32)
    add_definitions(-DJUCE_PLUGINHOST_VST -DJUCE_ASIO)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

file(GLOB EIM_SRC_FILES "src/*.cpp" "src/*.h")

add_subdirectory(JavaSharedMemory)

add_subdirectory(JUCE)
juce_set_ara_sdk_path(ARA_SDK)

juce_add_console_app(${PROJECT_NAME}
    VERSION "1.0.0"
    PRODUCT_NAME ${PROJECT_NAME}
    COMPANY_NAME "EchoInMirror"
    BUNDLE_ID com.eimsound.eimhost
    ICON_SMALL "resources/logo128.png"
    ICON_BIG "resources/logo512.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    PLUGINHOST_AU TRUE
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict><key>LSUIElement</key><string>1</string></dict></plist>"
)
set_target_properties(${PROJECT_NAME} PROPERTIES JUCE_TARGET_KIND_STRING "App")
_juce_configure_bundle(${PROJECT_NAME} ${PROJECT_NAME})
if(APPLE)
    _juce_configure_app_bundle(${PROJECT_NAME} ${PROJECT_NAME})
endif()

target_sources(${PROJECT_NAME} PRIVATE ${EIM_SRC_FILES})
target_compile_definitions(${PROJECT_NAME} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

if(WIN32)
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
endif()

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EIMHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EIMHost,JUCE_VERSION>")

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        java_shared_memory)
