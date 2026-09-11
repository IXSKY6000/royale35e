cmake_minimum_required(VERSION 3.15)
project(Royale35E VERSION 0.1 LANGUAGES C)

include(GenerateG1A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)

set(SOURCES
  src/main.c
)

add_executable(royale35e ${SOURCES})
target_compile_options(royale35e PRIVATE -Wall -Wextra -Os)
target_link_libraries(royale35e Gint::Gint)

generate_g1a(
  TARGET royale35e
  OUTPUT "ROYALE35.g1a"
  NAME "Royale35"
  ICON assets-fx/icon.png
)
