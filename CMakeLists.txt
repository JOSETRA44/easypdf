cmake_minimum_required (VERSION 3.14)

project ("easypdf")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Habilite Recarga activa para los compiladores de MSVC si se admiten.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

include(FetchContent)

# Dependencia: cpr (C++ Requests)
FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.1 # Versión estable
)
set(CPR_ENABLE_SSL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)

# Dependencia: nlohmann_json
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Dependencia: libharu (Generador de PDF)
FetchContent_Declare(
  libharu
  GIT_REPOSITORY https://github.com/libharu/libharu.git
  GIT_TAG v2.4.4
)
set(LIBHARU_ENABLE_PNG ON CACHE BOOL "" FORCE)
set(LIBHARU_ENABLE_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libharu)

# Incluya los subproyectos.
add_subdirectory ("easypdf")
