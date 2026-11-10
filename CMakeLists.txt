cmake_minimum_required(VERSION 3.21)
project(plasma-hdrvideo VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
    wayland-client wayland-egl egl mpv)

# --- nagłówki protokołów Wayland (staging/color-management, stable/viewporter)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WP_DIR wayland-protocols pkgdatadir)
set(GEN ${CMAKE_BINARY_DIR}/wl-gen)
file(MAKE_DIRECTORY ${GEN})

function(wl_protocol xml base)
    add_custom_command(
        OUTPUT ${GEN}/${base}-client-protocol.h ${GEN}/${base}-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${GEN}/${base}-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${GEN}/${base}-protocol.c
        DEPENDS ${xml} VERBATIM)
endfunction()

wl_protocol(${WP_DIR}/staging/color-management/color-management-v1.xml color-management-v1)
wl_protocol(${WP_DIR}/stable/viewporter/viewporter.xml viewporter)

qt_add_qml_module(hdrvideoplugin
    URI io.github.jocker3201.hdrvideo
    VERSION 1.0
    PLUGIN_TARGET hdrvideoplugin
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/io/github/jocker3201/hdrvideo
    SOURCES
        src/hdrvideosurface.h
        src/hdrvideosurface.cpp
        ${GEN}/color-management-v1-client-protocol.h
        ${GEN}/color-management-v1-protocol.c
        ${GEN}/viewporter-client-protocol.h
        ${GEN}/viewporter-protocol.c
)

# qmltyperegistrar nie wstawia include naszej klasy do wygenerowanej rejestracji —
# wymuszamy nagłówek dla całego targetu.
target_compile_options(hdrvideoplugin PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;${CMAKE_SOURCE_DIR}/src/hdrvideosurface.h>")

# Chimera nie dostarcza targetu Qt6::GuiPrivate, ale nagłówki private są na dysku —
# potrzebne wyłącznie dla qpa/qplatformnativeinterface.h (wl_surface okna).
target_include_directories(hdrvideoplugin PRIVATE ${GEN}
    /usr/include/qt6/QtGui/${Qt6Gui_VERSION}
    /usr/include/qt6/QtGui/${Qt6Gui_VERSION}/QtGui
    /usr/include/qt6/QtCore/${Qt6Core_VERSION}
    /usr/include/qt6/QtCore/${Qt6Core_VERSION}/QtCore)
target_link_libraries(hdrvideoplugin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml
    PkgConfig::DEPS)
