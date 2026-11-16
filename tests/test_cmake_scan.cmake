# ESP-IDF includes main/CMakeLists.txt in script mode before normal configure.
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
macro(idf_component_register)
endmacro()
macro(esptool_py_flash_to_partition)
endmacro()
include("${PROJECT_DIR}/main/CMakeLists.txt")
