include_directories(${FLTK_INCLUDE_DIR})
include_directories(${GETTEXT_INCLUDE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/unix)
include_directories(${CMAKE_SOURCE_DIR}/common)
set(X11CLONE_SOURCES
  menukey.cxx
  CConn.cxx
  DesktopWindow.cxx
  UserDialog.cxx
  ServerDialog.cxx
  Surface.cxx
  OptionsDialog.cxx
  PlatformPixelBuffer.cxx
  Viewport.cxx
  parameters.cxx
  keysym2ucs.c
  ../unix/x0vncserver/Geometry.cxx
  ../unix/x0vncserver/XDesktop.cxx
  ../unix/x0vncserver/Image.cxx
  ../unix/x0vncserver/XPixelBuffer.cxx
  ../unix/x0vncserver/PollingManager.cxx
  ../unix/x0vncserver/PollingScheduler.cxx
  ../unix/x0vncserver/TimeMillis.cxx
  ../unix/x0vncserver/qnum_to_xorgevdev.c
  ../unix/x0vncserver/qnum_to_xorgkbd.c
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
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} win32.c)
elseif(APPLE)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} cocoa.mm osx_to_qnum.c)
else()
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} xkb_to_qnum.c)
endif()

if(WIN32)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} Surface_Win32.cxx)
elseif(APPLE)
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} Surface_OSX.cxx)
else()
  set(X11CLONE_SOURCES ${X11CLONE_SOURCES} Surface_X11.cxx)
endif()

if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_executable(x11clone WIN32 ${X11CLONE_SOURCES})
else()
  add_executable(x11clone ${X11CLONE_SOURCES})
endif()

target_link_libraries(x11clone tx rfb network rdr os ${FLTK_LIBRARIES} ${GETTEXT_LIBRARIES})

if(X11_FOUND AND X11_XTest_LIB)
  add_definitions(-DHAVE_XTEST)
  target_link_libraries(x11clone ${X11_XTest_LIB})
else()
  message(WARNING "No XTest extension.  x11clone will be view-only.")
endif()

if(X11_FOUND AND X11_Xdamage_LIB)
  add_definitions(-DHAVE_XDAMAGE)
  target_link_libraries(x11clone ${X11_Xdamage_LIB})
else()
  message(WARNING "No DAMAGE extension.  x11clone will have to use the slower polling method.")
endif()

if(X11_FOUND AND X11_Xfixes_LIB)
  add_definitions(-DHAVE_XFIXES)
  target_link_libraries(x11clone ${X11_Xfixes_LIB})
else()
  message(WARNING "No XFIXES extension.  x11clone will not be able to show cursors.")
endif()

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
