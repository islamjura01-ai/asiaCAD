cmake_minimum_required(VERSION 3.20)
project(AsiaCAD VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W3 /utf-8 /bigobj /permissive-)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# utf8, json, kod sahifalari — past darajadagi primitivlar, core ichida
add_library(asiacad_core
  src/i18n/utf8.cpp
  src/i18n/json.cpp
  src/io/dxf_text.cpp
  src/core/entities.cpp
  src/core/document.cpp
  src/core/commands.cpp
  src/core/geom2d.cpp
  src/core/spatial_index.cpp
  src/core/snap.cpp
  src/core/dimension.cpp
  src/core/grips.cpp
  src/core/modify.cpp
  src/core/draft.cpp
  src/io/dxf_writer.cpp
  src/io/xlsx.cpp
)
target_include_directories(asiacad_core PUBLIC include)

# Core tarjima qilmaydi: faqat Error(kalit, argumentlar) chiqaradi; lug'atlar shu kutubxonada
add_library(asiacad_i18n
  src/i18n/localizer.cpp
)
target_include_directories(asiacad_i18n PUBLIC include)
target_link_libraries(asiacad_i18n PUBLIC asiacad_core)

# Fayl formatlari: DXF import, native .acad
add_library(asiacad_io
  src/io/dxf_reader.cpp
  src/io/acad_file.cpp
)
target_include_directories(asiacad_io PUBLIC include)
target_link_libraries(asiacad_io PUBLIC asiacad_core)

set(ASIACAD_I18N_SRC "${CMAKE_SOURCE_DIR}/i18n")

# Muhandislik modullari: quvurlar katalogi, kanalizatsiya tarmog'i
add_library(asiacad_civil
  src/civil/pipe_catalog.cpp
  src/civil/sewer.cpp
  src/bim/building.cpp
  src/bim/rebar.cpp
  src/bim/rebar_import.cpp
  src/bim/kj_project.cpp
  src/bim/steel.cpp
  src/bim/templates.cpp
  src/civil/excel.cpp
  src/civil/utility.cpp
  src/civil/road.cpp
  src/civil/waterway.cpp
  src/civil/corridor.cpp
  src/civil/drawset.cpp
  src/civil/generators.cpp
  src/civil/drive.cpp
  src/civil/symbols.cpp
  src/civil/survey.cpp
  src/civil/terrain.cpp
)
target_include_directories(asiacad_civil PUBLIC include)
target_link_libraries(asiacad_civil PUBLIC asiacad_core asiacad_io)

# Buyruq interpretatori: CLI va GUI uchun umumiy
add_library(asiacad_cmd apps/cli/interpreter.cpp apps/cli/interpreter_draft.cpp apps/cli/interpreter_survey.cpp apps/cli/interpreter_terrain.cpp)
target_include_directories(asiacad_cmd PUBLIC apps/cli)
target_link_libraries(asiacad_cmd PUBLIC asiacad_io asiacad_i18n asiacad_civil)

add_executable(asiacad apps/cli/main.cpp)
target_link_libraries(asiacad PRIVATE asiacad_cmd)
target_compile_definitions(asiacad PRIVATE ASIACAD_I18N_DIR="${ASIACAD_I18N_SRC}")
# Lug'atlarni dastur yoniga nusxalash (tarqatish uchun)
add_custom_command(TARGET asiacad POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASIACAD_I18N_SRC} $<TARGET_FILE_DIR:asiacad>/i18n)

enable_testing()
add_executable(asiacad_tests tests/test_main.cpp)
target_link_libraries(asiacad_tests PRIVATE asiacad_cmd)
target_compile_definitions(asiacad_tests PRIVATE ASIACAD_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
target_compile_definitions(asiacad_tests PRIVATE ASIACAD_I18N_DIR="${ASIACAD_I18N_SRC}" ASIACAD_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
add_test(NAME core COMMAND asiacad_tests)

# Grafik interfeys (Qt 6). Qt topilmasa faqat CLI yig'iladi.
option(ASIACAD_BUILD_GUI "Qt 6 grafik interfeysini yig'ish" ON)
if(ASIACAD_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets OpenGLWidgets)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(asiacad-gui
      apps/gui/main.cpp
      apps/gui/main_window.cpp
      apps/gui/viewport.cpp
      apps/gui/theme.cpp
      apps/gui/icons.cpp
      apps/gui/command_palette.cpp apps/gui/sewer_dialog.cpp apps/gui/pipe3d_view.cpp apps/gui/building_dialog.cpp apps/gui/rebar_dialog.cpp apps/gui/template_sidebar.cpp apps/gui/excel_link.cpp apps/gui/drawing_set_widget.cpp apps/gui/utility_dialog.cpp apps/gui/road_dialog.cpp apps/gui/waterway_dialog.cpp apps/gui/corridor_dialog.cpp apps/gui/generator_dialogs.cpp apps/gui/templates_dialog.cpp apps/gui/survey_dialog.cpp apps/gui/earthwork_dialog.cpp apps/gui/drive_view.cpp apps/gui/drive_dialog.cpp apps/gui/draft_panels.cpp
    )
    target_link_libraries(asiacad-gui PRIVATE asiacad_cmd Qt6::Widgets Qt6::OpenGLWidgets)
    target_compile_definitions(asiacad-gui PRIVATE ASIACAD_I18N_DIR="${ASIACAD_I18N_SRC}")
    add_custom_command(TARGET asiacad-gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASIACAD_I18N_SRC} $<TARGET_FILE_DIR:asiacad-gui>/i18n)
    if(WIN32)
      set_target_properties(asiacad-gui PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    install(TARGETS asiacad-gui RUNTIME DESTINATION bin)

    # GUI tutun testi (ekransiz)
    add_executable(asiacad_gui_tests tests/gui_smoke.cpp
      apps/gui/main_window.cpp apps/gui/viewport.cpp apps/gui/theme.cpp apps/gui/icons.cpp apps/gui/command_palette.cpp apps/gui/sewer_dialog.cpp apps/gui/pipe3d_view.cpp apps/gui/building_dialog.cpp apps/gui/rebar_dialog.cpp apps/gui/template_sidebar.cpp apps/gui/excel_link.cpp apps/gui/drawing_set_widget.cpp apps/gui/utility_dialog.cpp apps/gui/road_dialog.cpp apps/gui/waterway_dialog.cpp apps/gui/corridor_dialog.cpp apps/gui/generator_dialogs.cpp apps/gui/templates_dialog.cpp apps/gui/survey_dialog.cpp apps/gui/earthwork_dialog.cpp apps/gui/drive_view.cpp apps/gui/drive_dialog.cpp apps/gui/draft_panels.cpp)
    target_include_directories(asiacad_gui_tests PRIVATE apps/gui)
    target_link_libraries(asiacad_gui_tests PRIVATE asiacad_cmd Qt6::Widgets Qt6::OpenGLWidgets)
    target_compile_definitions(asiacad_gui_tests PRIVATE ASIACAD_I18N_DIR="${ASIACAD_I18N_SRC}" ASIACAD_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
    add_test(NAME gui COMMAND asiacad_gui_tests)
    set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  else()
    message(STATUS "Qt6 topilmadi — asiacad-gui yig'ilmaydi")
  endif()
endif()

install(TARGETS asiacad RUNTIME DESTINATION bin)
install(DIRECTORY i18n DESTINATION bin)
