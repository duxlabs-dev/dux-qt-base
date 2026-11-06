include(GNUInstallDirs)

# Every module links into the `quickshell` target, so the target name stays
# while the artifact becomes libdux-qt-base.so with a C entry point.
qt_add_library(quickshell SHARED capi.cpp)

set_target_properties(quickshell PROPERTIES
	OUTPUT_NAME dux-qt-base
	VERSION ${PROJECT_VERSION}
	SOVERSION 0
)

target_include_directories(quickshell PUBLIC
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(TARGETS quickshell LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/dux-qt-base DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(${CMAKE_SOURCE_DIR}/dux-qt-base.pc.in ${CMAKE_BINARY_DIR}/dux-qt-base.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/dux-qt-base.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if (BUILD_EXECUTABLE)
	qt_add_executable(dux-qt-base-bin main.cpp)
	set_target_properties(dux-qt-base-bin PROPERTIES OUTPUT_NAME dux-qt-base)
	target_link_libraries(dux-qt-base-bin PRIVATE quickshell)
	install(TARGETS dux-qt-base-bin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

add_subdirectory(build)
target_link_libraries(quickshell PRIVATE quickshell-build)
add_subdirectory(launch)
add_subdirectory(core)
add_subdirectory(debug)
add_subdirectory(ipc)
add_subdirectory(window)
add_subdirectory(io)
add_subdirectory(widgets)
add_subdirectory(ui)
add_subdirectory(windowmanager)

if (CRASH_HANDLER)
	add_subdirectory(crash)
endif()

if (DBUS)
	add_subdirectory(dbus)
endif()

if (WAYLAND)
	add_subdirectory(wayland)
endif()

if (X11)
	add_subdirectory(x11)
endif()

if (I3)
	add_subdirectory(x11/i3)
endif()

add_subdirectory(services)

if (BLUETOOTH)
	add_subdirectory(bluetooth)
endif()

if (NETWORK)
	add_subdirectory(network)
endif()
