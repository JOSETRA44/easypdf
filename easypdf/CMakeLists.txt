# CMakeList.txt para el ejecutable easypdf

add_executable (easypdf "easypdf.cpp" "easypdf.h")

target_link_libraries(easypdf PRIVATE cpr::cpr nlohmann_json::nlohmann_json hpdf)

# libharu no siempre exporta sus cabeceras correctamente en CMake antiguo o configuraciones específicas
target_include_directories(easypdf PRIVATE 
    ${libharu_SOURCE_DIR}/include
    ${libharu_BINARY_DIR}/include
)

# Copiar el archivo .env al directorio de salida si existe
configure_file("${CMAKE_SOURCE_DIR}/.env" "${CMAKE_CURRENT_BINARY_DIR}/.env" COPYONLY)
