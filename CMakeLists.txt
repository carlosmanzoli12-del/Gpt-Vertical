cmake_minimum_required(VERSION 3.16)
project(vertical-canvas-lite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---- Caminhos do OBS 27.2.4 (voce informa na hora de configurar) ----
set(OBS_SOURCE_DIR "" CACHE PATH "Pasta do codigo-fonte do obs-studio (tag 27.2.4)")
set(OBS_BUILD_DIR "" CACHE PATH "Pasta de build do obs-studio (ex: obs-studio/build64)")
set(OBS_CONFIG "Release" CACHE STRING "Configuracao usada ao compilar o OBS (Release ou RelWithDebInfo)")

if(NOT OBS_SOURCE_DIR OR NOT OBS_BUILD_DIR)
  message(FATAL_ERROR "Informe -DOBS_SOURCE_DIR=... e -DOBS_BUILD_DIR=...")
endif()

# CMAKE_PREFIX_PATH deve apontar para o Qt 5.15.2 (ex: C:/Qt/5.15.2/msvc2019_64)
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Widgets)

set(OBS_LIBOBS_LIB "${OBS_BUILD_DIR}/libobs/${OBS_CONFIG}/obs.lib")
set(OBS_FRONTEND_LIB
    "${OBS_BUILD_DIR}/UI/obs-frontend-api/${OBS_CONFIG}/obs-frontend-api.lib")

foreach(lib ${OBS_LIBOBS_LIB} ${OBS_FRONTEND_LIB})
  if(NOT EXISTS "${lib}")
    message(FATAL_ERROR "Nao encontrei: ${lib}\nConfira OBS_BUILD_DIR e OBS_CONFIG.")
  endif()
endforeach()

add_library(vertical-canvas-lite MODULE
  src/plugin-main.cpp
  src/vertical-canvas.cpp
  src/vertical-canvas.hpp
  src/preview-widget.cpp
  src/preview-widget.hpp
  src/vertical-dock.cpp
  src/vertical-dock.hpp)

set_target_properties(vertical-canvas-lite PROPERTIES PREFIX "")

target_include_directories(vertical-canvas-lite PRIVATE
  "${OBS_SOURCE_DIR}/libobs"
  "${OBS_SOURCE_DIR}/UI/obs-frontend-api"
  "${OBS_SOURCE_DIR}/deps/w32-pthreads"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(vertical-canvas-lite PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

if(MSVC)
  # Necessario: o codigo tem acentos em UTF-8
  target_compile_options(vertical-canvas-lite PRIVATE /utf-8)
endif()

target_link_libraries(vertical-canvas-lite PRIVATE
  Qt5::Core Qt5::Widgets
  "${OBS_LIBOBS_LIB}"
  "${OBS_FRONTEND_LIB}")

install(TARGETS vertical-canvas-lite
  RUNTIME DESTINATION obs-plugins/64bit
  LIBRARY DESTINATION obs-plugins/64bit)
