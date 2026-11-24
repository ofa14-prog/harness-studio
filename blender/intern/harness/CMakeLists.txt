# SPDX-FileCopyrightText: 2026 Harness Studio contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Harness Studio shell: Qt Quick window, Blender bridge, AI studio, profiles.

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Svg Quick3D)

set(INC
  .
  ../ghost
  ../ghost/intern
)

set(INC_SYS
)

set(SRC
  harness_bridge.cc
  harness_shell.cc
  gltf_model.cc
  studio/profiles.cc
  studio/studio_agent.cc
  studio/studio_create.cc
  studio/studio_models.cc

  harness_bridge.hh
  harness_shell.hh
  gltf_model.hh
  studio/profiles.hh
  studio/studio.hh
)

# One resource file with the QML, our icons/fonts/data and Blender's own icons.
file(GLOB _qml RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml qml/qmldir)
file(GLOB_RECURSE _res RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/*.svg resources/*.ttf resources/*.json resources/*.jpg resources/*.png)
file(GLOB _bicons ${CMAKE_SOURCE_DIR}/release/datafiles/icons_svg/*.svg)
set(_qrc "${CMAKE_CURRENT_BINARY_DIR}/harness.qrc")
set(_body "<RCC>\n  <qresource prefix=\"/harness\">\n")
foreach(f ${_qml})
  string(APPEND _body "    <file alias=\"${f}\">${CMAKE_CURRENT_SOURCE_DIR}/${f}</file>\n")
endforeach()
foreach(f ${_res})
  string(REPLACE "resources/" "" _alias "${f}")
  string(APPEND _body "    <file alias=\"${_alias}\">${CMAKE_CURRENT_SOURCE_DIR}/${f}</file>\n")
endforeach()
foreach(f ${_bicons})
  get_filename_component(_n ${f} NAME)
  string(APPEND _body "    <file alias=\"blender-icons/${_n}\">${f}</file>\n")
endforeach()
string(APPEND _body "  </qresource>\n</RCC>\n")
file(WRITE ${_qrc}.in "${_body}")
configure_file(${_qrc}.in ${_qrc} COPYONLY)
list(APPEND SRC ${_qrc})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_qml} ${_res})

set(LIB
  PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Svg Qt6::Quick3D
  PRIVATE bf::dependencies::optional::python
)

add_definitions(-DQT_NO_KEYWORDS)

blender_add_lib(bf_intern_harness "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
set_target_properties(bf_intern_harness PROPERTIES AUTOMOC ON AUTORCC ON)
