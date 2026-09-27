cmake_minimum_required(VERSION 3.15)

project(vSID VERSION "0.8.0" LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0156 NEW)

# use download time as timestamp for archives extracted by FetchContent (CMake 3.24+)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ensure 32-bit (Win32) build (ES limitation)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "vSID must be built as 32-bit. Configure with -A Win32.")
endif()

# force static CRT for entire project
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# --- configure libcurl (see external/curl handling below) ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)

# Use Windows native SSL, no OpenSSL DLLs
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)

# Only HTTP/HTTPS – no other protocols
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

# Do NOT require libpsl
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

# ---- Strip unused features ----
# No alternative services, HSTS, cookies etc.
set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_HSTS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_COOKIES ON CACHE BOOL "" FORCE)

# No protocols beyond HTTP(S)
set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DOH ON CACHE BOOL "" FORCE)   # no DNS-over-HTTPS
set(CURL_DISABLE_IPFS ON CACHE BOOL "" FORCE)

# No mail / legacy / misc protocols
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)

# No forms/MIME APIs
set(CURL_DISABLE_MIME ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FORM_API ON CACHE BOOL "" FORCE)

# No progress meter / verbose strings / netrc / date parsing
set(CURL_DISABLE_PROGRESS_METER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_VERBOSE_STRINGS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_NETRC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_PARSEDATE ON CACHE BOOL "" FORCE)

# auth methods
set(CURL_DISABLE_BASIC_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_BEARER_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DIGEST_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_KERBEROS_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_NEGOTIATE_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_AWS ON CACHE BOOL "" FORCE)

# libcurl lives in external/curl. The build workflow pre-populates that path; for
# local builds it is downloaded here automatically (same version + hash as the workflow).
set(VSID_CURL_VERSION "8.17.0" CACHE STRING "libcurl version to fetch when external/curl is missing")
set(VSID_CURL_SHA256 "0be15247c07422a0061fda4758c0182bb9c688e40ecf968d1a0b92742a32667e"
    CACHE STRING "SHA256 of the libcurl archive matching VSID_CURL_VERSION")
mark_as_advanced(VSID_CURL_VERSION VSID_CURL_SHA256)

set(VSID_CURL_DIR "${CMAKE_SOURCE_DIR}/external/curl")
set(VSID_CURL_BUILD_DIR "${CMAKE_BINARY_DIR}/external/curl")

if(EXISTS "${VSID_CURL_DIR}/CMakeLists.txt")
    message(STATUS "Using libcurl source already present in ${VSID_CURL_DIR}")
    add_subdirectory("${VSID_CURL_DIR}" "${VSID_CURL_BUILD_DIR}")
else()
    string(REPLACE "." "_" _curl_tag "curl-${VSID_CURL_VERSION}")
    message(STATUS "Fetching libcurl ${VSID_CURL_VERSION} into ${VSID_CURL_DIR}")

    include(FetchContent)
    FetchContent_Declare(curl
        URL "https://github.com/curl/curl/archive/refs/tags/${_curl_tag}.zip"
        URL_HASH SHA256=${VSID_CURL_SHA256}
        SOURCE_DIR "${VSID_CURL_DIR}"
        BINARY_DIR "${VSID_CURL_BUILD_DIR}"
    )
    FetchContent_MakeAvailable(curl)
endif()

# directory for additional linkage
link_directories(${CMAKE_SOURCE_DIR}/include/es)

# directory where headers are stored
include_directories(${CMAKE_SOURCE_DIR}/include)

# DLL source files
set(SOURCES 
    autoconfiguration.cpp
    autoconfiguration_plugin.cpp
    bridgeconfiguration_plugin.cpp
    area.cpp
    configparser.cpp
    crashhandler.cpp
    eseparser.cpp
    display.cpp
    flightplan.cpp
    menu.cpp
    messageHandler.cpp
    logger.cpp
    pch.cpp
    sid.cpp
    syncManager.cpp
    timeHandler.cpp
    utils.cpp
    versionchecker.cpp
    vSID.cpp
    vSIDPlugin.cpp
)

# DLL target
add_library(vSID SHARED ${SOURCES})

# target_compile_options(vSID PRIVATE -O2) old

if(MSVC)
    # create debug info - /Oy prevents frame pointer omission (for debugging)
    target_compile_options(vSID PRIVATE /Zi "$<$<CONFIG:Release>:/Oy->")

    # created PDB - /MAP maps function offsets
    target_link_options(vSID PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF "$<$<CONFIG:Release>:/MAP>")
endif()

# set to use MSVC - maybe old
# set_property(TARGET vSID PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")


# link curl statically
target_compile_definitions(vSID PRIVATE CURL_STATICLIB)

# NEW: choose the correct curl target and link it, plus ES lib
# Some curl CMake setups export CURL::libcurl, others just libcurl
if(TARGET CURL::libcurl)
    set(_curl_target CURL::libcurl)
elseif(TARGET libcurl)
    set(_curl_target libcurl)
else()
    message(FATAL_ERROR "libcurl target not found after add_subdirectory(external/curl)")
endif()

target_link_libraries(vSID
    PRIVATE
        ${_curl_target}
        EuroScopePlugInDLL.lib
        dbghelp
)

include(CTest)
if(BUILD_TESTING)
    add_executable(bridgeconfiguration_test tests/bridgeconfiguration_test.cpp)
    add_test(NAME bridgeconfiguration COMMAND bridgeconfiguration_test)
    add_executable(autoconfiguration_test tests/autoconfiguration_test.cpp autoconfiguration.cpp)
    add_test(NAME autoconfiguration COMMAND autoconfiguration_test "${CMAKE_SOURCE_DIR}/vSidAutoConfig.json")
endif()

# Keep the editable Auto configuration beside the built DLL.
add_custom_target(vsid_runtime_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/vSidAutoConfig.json"
        "${CMAKE_SOURCE_DIR}/AUTO_CONFIGURATION.md"
        "$<TARGET_FILE_DIR:vSID>"
    DEPENDS vSID
    VERBATIM
)
