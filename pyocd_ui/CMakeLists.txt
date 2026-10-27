cmake_minimum_required(VERSION 3.16)

project(pyocd_ui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(pyocd_ui
    main.cpp
    mainwindow.cpp
    PyOcdRunner.cpp
    PyocdLocator.cpp
    PackParser.cpp
    FirmwareParser.cpp
    SectorSelectorDialog.cpp
)

target_include_directories(pyocd_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pyocd_ui PRIVATE Qt6::Widgets)

if(WIN32)
    set_target_properties(pyocd_ui PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ---------------------------------------------------------------------------
# MinGW 运行时 DLL 放到 exe 旁边。
# Windows 的 DLL 搜索顺序是 "exe 目录 -> System32 -> Windows -> PATH"，
# PATH 里可能躺着旧版 libstdc++(例如 GTK3-Runtime Win64\bin)，
# 会导致 Qt6Gui.dll 报 "无法定位程序输入点 _ZSt21__glibcxx_assert_fail..."(0xC0000139)。
# 放在 exe 目录即可彻底免疫 PATH。
# ---------------------------------------------------------------------------
get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(MINGW_RUNTIME_DIR "${_compiler_dir}" CACHE PATH
    "MinGW 运行时 DLL 目录(libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll)")

foreach(_rt libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
    if(EXISTS "${MINGW_RUNTIME_DIR}/${_rt}")
        add_custom_command(TARGET pyocd_ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_RUNTIME_DIR}/${_rt}"
                    $<TARGET_FILE_DIR:pyocd_ui>/${_rt})
    endif()
endforeach()
