include_directories(${FLTK_INCLUDE_DIR})
include_directories(${GETTEXT_INCLUDE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/unix/common)
include_directories(${CMAKE_SOURCE_DIR}/unix)
include_directories(${CMAKE_SOURCE_DIR}/common)
set(X11CLONE_SOURCES
  ../../vncviewer/menukey.cxx
  CConn.cxx
  DesktopWindow.cxx
  UserDialog.cxx
  ServerDialog.cxx
  OptionsDialog.cxx
  ../../vncviewer/Surface.cxx
  ../../vncviewer/Surface_X11.cxx
  ../../vncviewer/PlatformPixelBuffer.cxx
  Viewport.cxx
  parameters.cxx
  ../../vncviewer/xkb_to_qnum.c
  ../x0vncserver/Geometry.cxx
  ../x0vncserver/XDesktop.cxx
  ../x0vncserver/RandrGlue.c
  ../x0vncserver/Image.cxx
  ../x0vncserver/XPixelBuffer.cxx
  ../x0vncserver/PollingManager.cxx
  ../x0vncserver/PollingScheduler.cxx
  ../x0vncserver/TimeMillis.cxx
  ../x0vncserver/qnum_to_xorgevdev.c
  ../x0vncserver/qnum_to_xorgkbd.c
  x11clone.cxx)

add_executable(x11clone ${X11CLONE_SOURCES})

target_link_libraries(x11clone tx rfb network rdr unixcommon os ${FLTK_LIBRARIES} ${GETTEXT_LIBRARIES})

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

if(X11_FOUND AND X11_Xrandr_LIB)
  add_definitions(-DHAVE_XRANDR)
  target_link_libraries(x11clone ${X11_Xrandr_LIB})
else()
  message(WARNING "No Xrandr extension.  x11clone will not be able to resize session.")
endif()

if(X11_FOUND AND X11_Xau_LIB)
  add_definitions(-DHAVE_XAU)
  target_link_libraries(x11clone ${X11_Xau_LIB})
endif()

install(TARGETS x11clone DESTINATION ${BIN_DIR})

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


set(ICON_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_16.png
  ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_22.png
  ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_24.png
  ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_32.png
  ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_48.png)
add_custom_target(x11cloneicons DEPENDS ${ICON_FILES})
find_program(CONVERT_EXECUTABLE convert)

install(FILES x11clone.svg DESTINATION ${DATA_DIR}/icons/hicolor/scalable/apps)

foreach(SIZE 16 22 24 32 48)

  install(FILES icons/x11clone_${SIZE}.png
    DESTINATION ${DATA_DIR}/icons/hicolor/${SIZE}x${SIZE}/apps
    RENAME x11clone.png)

  if(CONVERT_EXECUTABLE)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_${SIZE}.png
      COMMAND ${CONVERT_EXECUTABLE} -density 72 -resize ${SIZE}x${SIZE} -gravity center -background transparent
        ${CMAKE_CURRENT_SOURCE_DIR}/x11clone.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/x11clone_${SIZE}.png
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/x11clone.svg
      COMMENT "Generating icons/x11clone_${SIZE}.png")
  endif()

endforeach()


file(STRINGS ../../po/LINGUAS po_FILES)
foreach(lang ${po_FILES})
  set(mo "../../po/${lang}.mo")

  install(FILES ${mo}
    DESTINATION "${LOCALE_DIR}/${lang}/LC_MESSAGES"
    RENAME x11clone.mo
  )

  set(moFiles ${moFiles} ${mo})
endforeach(lang)

add_custom_target(x11clonetranslations ALL DEPENDS translations)

configure_file(maketarball.in maketarball)
set(TARBALL_DEPENDS x11clone)
add_custom_target(x11clone-tarball sh maketarball
  DEPENDS ${TARBALL_DEPENDS}
  SOURCES maketarball)
