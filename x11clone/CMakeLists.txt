include_directories(${FLTK_INCLUDE_DIR})
include_directories(${GETTEXT_INCLUDE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_SOURCE_DIR}/vncviewer)
set(X11CLONE_SOURCES
  ../vncviewer/menukey.cxx
  ../vncviewer/CConn.cxx
  ../vncviewer/DesktopWindow.cxx
  ../vncviewer/UserDialog.cxx
  ServerDialog.cxx
  ../vncviewer/Surface.cxx
  OptionsDialog.cxx
  ../vncviewer/PlatformPixelBuffer.cxx
  Viewport.cxx
  ../vncviewer/parameters.cxx
  ../vncviewer/keysym2ucs.c
  x11clone.cxx)

if(WIN32)
  # Since x11clone.rc is generated, local includes will be looking
  # in the wrong directory. We need to help it out.
  include_directories(${CMAKE_CURRENT_SOURCE_DIR})
  configure_file(x11clone.rc.in x11clone.rc)
  set(X11CLONE_SOURCES
    ${X11CLONE_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/x11clone.rc)
endif()

if(WIN32)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} ../vncviewer/win32.c)
elseif(APPLE)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} ../vncviewer/cocoa.mm ../vncviewer/osx_to_qnum.c)
else()
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} ../vncviewer/xkb_to_qnum.c)
endif()

if(WIN32)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} ../vncviewer/Surface_Win32.cxx)
elseif(APPLE)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} ../vncviewer/Surface_OSX.cxx)
else()
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} ../vncviewer/Surface_X11.cxx)
endif()

if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_executable(x11clone WIN32 ${X11CLONE_SOURCES})
else()
  add_executable(x11clone ${X11CLONE_SOURCES})
endif()

target_link_libraries(x11clone rfb network rdr os ${FLTK_LIBRARIES} ${GETTEXT_LIBRARIES})

if(WIN32)
  target_link_libraries(x11clone msimg32)
endif()

if(APPLE)
  target_link_libraries(x11clone "-framework Cocoa")
  target_link_libraries(x11clone "-framework Carbon")
  target_link_libraries(x11clone "-framework IOKit")
endif()

install(TARGETS x11clone DESTINATION ${BIN_DIR})
if(UNIX)
  install(FILES x11clone.man DESTINATION ${MAN_DIR}/man1 RENAME x11clone.1)

  configure_file(x11clone.desktop.in.in x11clone.desktop.in)
  find_program(INTLTOOL_MERGE_EXECUTABLE intltool-merge)
  if("${GETTEXT_VERSION_STRING}" VERSION_GREATER 0.18.99)
    add_custom_command(OUTPUT x11clone.desktop
      COMMAND ${GETTEXT_MSGFMT_EXECUTABLE}
                --desktop --template x11clone.desktop.in
                -d ${CMAKE_SOURCE_DIR}/po -o x11clone.desktop
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/x11clone.desktop.in
    )
  elseif(INTLTOOL_MERGE_EXECUTABLE)
    add_custom_command(OUTPUT x11clone.desktop
      COMMAND sed -e 's/^Name/_Name/'
                -e 's/^GenericName/_GenericName/'
                -e 's/^Comment/_Comment/'
                x11clone.desktop.in > x11clone.desktop.intl
      COMMAND ${INTLTOOL_MERGE_EXECUTABLE}
                -d ${CMAKE_SOURCE_DIR}/po
                x11clone.desktop.intl x11clone.desktop
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/x11clone.desktop.in
    )
  else()
    add_custom_command(OUTPUT x11clone.desktop
      COMMAND cp x11clone.desktop.in x11clone.desktop
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/x11clone.desktop.in
    )
  endif()
  add_custom_target(x11clonedesktop ALL DEPENDS x11clone.desktop)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/x11clone.desktop DESTINATION ${DATA_DIR}/applications)

  foreach(res 16 22 24 32 48)
    install(FILES ../media/icons/tigervnc_${res}.png DESTINATION ${DATA_DIR}/icons/hicolor/${res}x${res}/apps RENAME tigervnc.png)
  endforeach()
  install(FILES ../media/icons/tigervnc.svg DESTINATION ${DATA_DIR}/icons/hicolor/scalable/apps)
endif()
