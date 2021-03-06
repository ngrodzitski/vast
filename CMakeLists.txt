cmake_minimum_required(VERSION 3.14...3.19 FATAL_ERROR)

# -- project setup -------------------------------------------------------------

# Semicolon-separated list of directories specifying a search path for CMake.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Helper utility for retrieving the project version from 'git-describe', that is
# bundled with VAST.
set(VAST_VERSION_FALLBACK "2021.01.28-0-")
include(VASTGitVersion)

# Create the VAST project.
project(
  VAST
  VERSION
    ${VAST_VERSION_MAJOR}.${VAST_VERSION_MINOR}.${VAST_VERSION_PATCH}.${VAST_VERSION_TWEAK}
  DESCRIPTION "Visibility Across Space and Time"
  LANGUAGES C CXX)

# Strip - and -0- suffixes from the version tag.
string(REGEX REPLACE "^(.*)[-0-|-]\\\$" "\\1" VAST_VERSION_TAG
                     "${VAST_VERSION_TAG}")

# -- sanity checks -------------------------------------------------------------

# Prohibit in-source builds.
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR "In-source builds are not allowed.")
endif ()

# Ensure that CMAKE_INSTALL_PREFIX is not a relative path.
if (NOT IS_ABSOLUTE "${CMAKE_INSTALL_PREFIX}")
  message(
    FATAL_ERROR
      "CMAKE_INSTALL_PREFIX must be an absolute path: ${CMAKE_INSTALL_PREFIX}")
endif ()

# -- build tree hash -----------------------------------------------------------

file(
  GLOB_RECURSE
  hash_files
  CONFIGURE_DEPENDS
  "libvast/*.hpp"
  "libvast/*.hpp.in"
  "libvast/*.cpp"
  "cmake/*"
  "CMakeLists.txt")

list(FILTER hash_files EXCLUDE REGEX
     "^${CMAKE_CURRENT_SOURCE_DIR}/libvast/aux/")

list(FILTER hash_files EXCLUDE REGEX
     "^${CMAKE_CURRENT_SOURCE_DIR}/libvast/test/")

list(
  SORT hash_files
  COMPARE FILE_BASENAME
  CASE INSENSITIVE)

foreach (hash_file IN LISTS hash_files)
  if (EXISTS "${hash_file}" AND NOT IS_DIRECTORY "${hash_file}")
    file(MD5 "${hash_file}" hash)
    list(APPEND hashes "${hash}")
  endif ()
endforeach ()

string(MD5 VAST_BUILD_TREE_HASH "${hashes}")

# -- includes ------------------------------------------------------------------

include(CMakeDependentOption)
include(CMakePackageConfigHelpers)
include(CMakePrintHelpers)
include(CPack)
include(CTest)
include(CheckCXXSourceCompiles)
include(CheckLibraryExists)
include(FeatureSummary)
include(GNUInstallDirs)
include(VASTCheckCompilerVersion)
include(VASTMacDependencyPaths)
include(VASTProvideFindModule)
include(VASTUtilities)

# -- project configuration -----------------------------------------------------

# Determine whether we're building VAST a subproject. This is used to determine
# good default values for many options. VAST should not modify global CMake if
# built as a subproject, unless explicitly requested to do so with options.
get_directory_property(_VAST_PARENT_DIRECTORY PARENT_DIRECTORY)
if (_VAST_PARENT_DIRECTORY)
  set(VAST_IS_SUBPROJECT ON)
  set(VAST_IS_NOT_SUBPROJECT OFF)
else ()
  set(VAST_IS_SUBPROJECT OFF)
  set(VAST_IS_NOT_SUBPROJECT ON)
endif ()
unset(_VAST_PARENT_DIRECTORY)

# -- library flavor ------------------------------------------------------------

option(BUILD_SHARED_LIBS "Build shared instead of static libraries" ON)
add_feature_info("BUILD_SHARED_LIBS" BUILD_SHARED_LIBS
                 "build shared instead of static libraries.\n")

# -- relocatable installations ----------------------------------------------

# Setting this option removes configuration dependent absolute paths from the
# VAST installation. Concretely, it enables the dynamic binary and libraries to
# use relative paths for loading their dependencies.
option(VAST_ENABLE_RELOCATABLE_INSTALLATIONS "Enable relocatable installations"
       ON)
add_feature_info(
  "VAST_ENABLE_RELOCATABLE_INSTALLATIONS" VAST_ENABLE_RELOCATABLE_INSTALLATIONS
  "enable relocatable installations.")

# -- silence policy-related warnings -------------------------------------------

# TODO: When updating to CAF 0.18, this should no longer be needed.
if (POLICY CMP0048)
  unset(PROJECT_VERSION)
  unset(PROJECT_VERSION_MAJOR)
  unset(PROJECT_VERSION_MINOR)
  unset(PROJECT_VERSION_PATCH)
  unset(PROJECT_VERSION_TWEAK)
endif ()

if (POLICY CMP0042)
  if (APPLE AND NOT DEFINED CMAKE_MACOSX_RPATH)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    # TODO: Verify that this is indeed correct.
    if (NOT VAST_ENABLE_RELOCATABLE_INSTALLATIONS)
      set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif ()
  endif ()
endif ()

# -- internal target -----------------------------------------------------------

# Create the internal target that is used for option/property propagation.
add_library(libvast_internal INTERFACE)
set_target_properties(libvast_internal PROPERTIES EXPORT_NAME internal)
add_library(vast::internal ALIAS libvast_internal)
install(TARGETS libvast_internal EXPORT VASTTargets)

# Require standard C++17
target_compile_features(libvast_internal INTERFACE cxx_std_17)

# Increase maximum number of template instantiations.
target_compile_options(libvast_internal INTERFACE -ftemplate-backtrace-limit=0)

# -- FreeBSD support -----------------------------------------------------------

if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  # Works around issues with libstdc++ and C++11. For details, see: -
  # https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=194929 -
  # https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=182657
  target_compile_definitions(
    libvast_internal INTERFACE _GLIBCXX_USE_C99 _GLIBCXX_USE_C99_MATH
                               _GLIBCXX_USE_C99_MATH_TR1)
endif ()

if (BUILD_SHARED_LIBS AND "${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
  target_link_libraries(libvast_internal INTERFACE dl)
endif ()

# -- optimizations -------------------------------------------------------------

# TODO add option to disable SSE

target_compile_options(
  libvast_internal
  INTERFACE $<$<CONFIG:Release>:-msse3
            -mssse3
            -msse4.1
            -msse4.2
            -fno-omit-frame-pointer>
            $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:CI>:-O2
            -g1>)

# -- warnings ------------------------------------------------------------------

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(libvast_internal INTERFACE -Wno-unknown-warning-option)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(libvast_internal INTERFACE -Wno-unknown-warning)
endif ()

target_compile_options(
  libvast_internal
  INTERFACE -Wall
            -Wextra
            -pedantic
            -Werror=switch
            -Wundef
            $<$<CONFIG:CI>:-Werror
            -Wno-error=deprecated
            -Wno-error=deprecated-declarations>
            $<$<CXX_COMPILER_ID:GNU>:-Wno-redundant-move>)

# -- build id ------------------------------------------------------------------

cmake_dependent_option(
  VAST_ENABLE_BUILDID "Include a unique identifier in the elf notes section" ON
  "CMAKE_EXECUTABLE_FORMAT STREQUAL ELF" OFF)
add_feature_info("VAST_ENABLE_BUILDID" VAST_ENABLE_BUILDID
                 "a unique identifier in the ELF notes section.")
if (VAST_ENABLE_BUILDID)
  target_link_options(libvast_internal INTERFACE "-Wl,--build-id")
endif ()

# -- USDT tracepoints ----------------------------------------------------------

option(VAST_ENABLE_SDT "Generate USDT tracepoint instrumentation" ON)
add_feature_info("VAST_ENABLE_SDT" VAST_ENABLE_SDT
                 "generate USDT tracepoint instrumentation.")

# -- build profiling -----------------------------------------------------------

option(VAST_ENABLE_TIME_REPORT
       "Print information where time was spent during compilation" OFF)
add_feature_info("VAST_ENABLE_TIME_REPORT" VAST_ENABLE_TIME_REPORT
                 "print information where time was spent during compilation.")
if (VAST_ENABLE_TIME_REPORT)
  target_compile_options(libvast_internal INTERFACE "-ftime-report")
endif ()

cmake_dependent_option(
  VAST_ENABLE_TIME_TRACE "Generate tracing JSON for compilation time profiling"
  OFF "CMAKE_CXX_COMPILER_ID MATCHES Clang" OFF)
add_feature_info("VAST_ENABLE_TIME_TRACE" VAST_ENABLE_TIME_TRACE
                 "generate tracing JSON for compilation time profiling.")
if (VAST_ENABLE_TIME_TRACE)
  target_compile_options(libvast_internal INTERFACE "-ftime-trace")
endif ()

# -- assertions ----------------------------------------------------------------

if (CMAKE_BUILD_TYPE STREQUAL Release)
  set(_VAST_ENABLE_ASSERTIONS_DEFAULT OFF)
else ()
  set(_VAST_ENABLE_ASSERTIONS_DEFAULT ON)
endif ()

option(VAST_ENABLE_ASSERTIONS "Enable assertions"
       "${_VAST_ENABLE_ASSERTIONS_DEFAULT}")
add_feature_info("VAST_ENABLE_ASSERTIONS" VAST_ENABLE_ASSERTIONS
                 "enable assertions.")

unset(_VAST_ENABLE_ASSERTIONS_DEFAULT)

# -- backtrace -----------------------------------------------------------------

option(VAST_ENABLE_BACKTRACE "Print a backtrace on unexpected termination" ON)
add_feature_info("VAST_ENABLE_BACKTRACE" VAST_ENABLE_BACKTRACE
                 "print a backtrace on unexpected termination.")
if (VAST_ENABLE_BACKTRACE)
  find_package(Backtrace)
  if (Backtrace_FOUND)
    target_include_directories(libvast_internal
                               INTERFACE ${Backtrace_INCLUDE_DIRECTORIES})
    target_link_libraries(libvast_internal INTERFACE ${Backtrace_LIBRARIES})
    dependency_summary("backtrace" ${Backtrace_INCLUDE_DIR})
  else ()
    find_package(libunwind REQUIRED)
    target_link_libraries(libvast_internal INTERFACE unwind::unwind)
    dependency_summary("libunwind" unwind::unwind)
  endif ()
endif ()

# -- sanitizers ----------------------------------------------------------------

# Address Sanitizer
option(VAST_ENABLE_ASAN
       "Insert pointer and reference checks into the generated binaries" OFF)
add_feature_info(
  "VAST_ENABLE_ASAN" VAST_ENABLE_ASAN
  "inserts pointer and reference checks into the generated binaries.")
if (VAST_ENABLE_ASAN)
  target_compile_options(libvast_internal INTERFACE -fsanitize=address
                                                    -fno-omit-frame-pointer)
  target_link_libraries(libvast_internal INTERFACE -fsanitize=address)
endif ()

# Undefined Behavior Sanitizer
option(VAST_ENABLE_UBSAN
       "Add run-time checks for undefined behavior into the generated binaries"
       OFF)
add_feature_info(
  "VAST_ENABLE_UBSAN" VAST_ENABLE_UBSAN
  "adds run-time checks for undefined behavior into the generated binaries.")
if (VAST_ENABLE_UBSAN)
  target_compile_options(libvast_internal INTERFACE -fsanitize=undefined)
  target_link_libraries(libvast_internal INTERFACE -fsanitize=undefined)
endif ()

# -- static build support -----------------------------------

cmake_dependent_option(
  VAST_ENABLE_STATIC_EXECUTABLE "Link VAST statically."
  "$ENV{VAST_ENABLE_STATIC_EXECUTABLE}" "NOT BUILD_SHARED_LIBS" OFF)
add_feature_info("VAST_ENABLE_STATIC_EXECUTABLE" VAST_ENABLE_STATIC_EXECUTABLE
                 "link VAST statically.")
if (VAST_ENABLE_STATIC_EXECUTABLE)
  target_link_libraries(libvast_internal INTERFACE -static-libgcc
                                                   -static-libstdc++ -static)
endif ()

if (VAST_ENABLE_STATIC_EXECUTABLE AND BUILD_SHARED_LIBS)
  message(FATAL_ERROR "Cannot create static binary with dynamic libraries")
endif ()

if (VAST_ENABLE_RELOCATABLE_INSTALLATIONS AND NOT VAST_ENABLE_STATIC_EXECUTABLE)
  # Note that we cannot use the target property INSTALL_RPATH on
  # libvast_internal, because it is an interface library. We should probably fix
  # this by making the config header part of libvast_internal so it is not just
  # an interface library.
  if (APPLE)
    list(PREPEND CMAKE_INSTALL_RPATH
         "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
  else ()
    list(PREPEND CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif ()
endif ()

# -- jemalloc ------------------------------------------------------------------

option(VAST_ENABLE_JEMALLOC "Use jemalloc instead of libc malloc"
       "${VAST_ENABLE_STATIC_EXECUTABLE}")
add_feature_info("VAST_ENABLE_JEMALLOC" VAST_ENABLE_JEMALLOC
                 "use jemalloc instead of libc malloc.")
if (VAST_ENABLE_JEMALLOC)
  find_package(jemalloc REQUIRED)
  provide_find_module(jemalloc)
  string(APPEND VAST_FIND_DEPENDENCY_LIST "\nfind_package(jemalloc REQUIRED)")
  target_link_libraries(libvast_internal INTERFACE jemalloc::jemalloc_)
  dependency_summary("jemalloc" jemalloc::jemalloc_)
endif ()

# -- developer mode ------------------------------------------------------------

# Build VAST in developer mode. This is enabled by default when not building
# VAST as a subproject. The developer mode contains CCache support and many
# other niceties.
option(VAST_ENABLE_DEVELOPER_MODE
       "Enables build settings for a nicer development environment"
       "${VAST_IS_NOT_SUBPROJECT}")
add_feature_info("VAST_ENABLE_DEVELOPER_MODE" VAST_ENABLE_DEVELOPER_MODE
                 "enables build settings for a nicer development environment.")

if (VAST_ENABLE_DEVELOPER_MODE)
  # Support tools like clang-tidy by creating a compilation database.
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  # Link the compilation database to the project root by default.
  add_custom_target(
    link_compilation_database ALL
    COMMAND
      ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json" compile_commands.json
    BYPRODUCTS compile_commands.json
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Linking compilation database")

  # Force colored output for the Ninja generator
  if ("${CMAKE_GENERATOR}" MATCHES "Ninja")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
      target_compile_options(libvast_internal
                             INTERFACE -fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
      target_compile_options(libvast_internal INTERFACE -fcolor-diagnostics)
    endif ()
  endif ()

  # Keep make output sane
  set(CMAKE_VERBOSE_MAKEFILE
      OFF
      CACHE STRING "Show all outputs including compiler lines." FORCE)

  # Enable CAF's compile-time type ID checks.
  target_compile_definitions(libvast_internal
                             INTERFACE CAF_ENABLE_TYPE_ID_CHECKS)
endif ()

if (VAST_ENABLE_DEVELOPER_MODE OR VAST_ENABLE_BUILDID)
  # Relocate debug paths to a common prefix for CCache users that work from
  # multiple worktrees.
  # The debug paths affect the build-id, we rewrite them to get a more
  # reproducible build.
  target_compile_options(
    libvast_internal
    INTERFACE "-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
endif ()

# -- additional warnings -------------------------------------------------------

option(VAST_ENABLE_MORE_WARNINGS "Enable addditional warnings" OFF)
add_feature_info("VAST_ENABLE_MORE_WARNINGS" VAST_ENABLE_MORE_WARNINGS
                 "enable additional warnings.")
if (VAST_ENABLE_MORE_WARNINGS)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(
      libvast_internal
      INTERFACE -Weverything
                -Wno-c++98-compat
                -Wno-padded
                -Wno-documentation-unknown-command
                -Wno-exit-time-destructors
                -Wno-global-constructors
                -Wno-missing-prototypes
                -Wno-c++98-compat-pedantic
                -Wno-unused-member-function
                -Wno-unused-const-variable
                -Wno-switch-enum
                -Wno-abstract-vbase-init
                -Wno-missing-noreturn
                -Wno-covered-switch-default)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(
      libvast_internal
      INTERFACE -Waddress
                -Wall
                -Warray-bounds
                -Wattributes
                -Wbuiltin-macro-redefined
                -Wcast-align
                -Wcast-qual
                -Wchar-subscripts
                -Wclobbered
                -Wcomment
                -Wconversion
                -Wconversion-null
                -Wcoverage-mismatch
                -Wcpp
                -Wdelete-non-virtual-dtor
                -Wdeprecated
                -Wdeprecated-declarations
                -Wdiv-by-zero
                -Wdouble-promotion
                -Wempty-body
                -Wendif-labels
                -Wenum-compare
                -Wextra
                -Wfloat-equal
                -Wformat
                -Wfree-nonheap-object
                -Wignored-qualifiers
                -Winit-self
                -Winline
                -Wint-to-pointer-cast
                -Winvalid-memory-model
                -Winvalid-offsetof
                -Wlogical-op
                -Wmain
                -Wmaybe-uninitialized
                -Wmissing-braces
                -Wmultichar
                -Wnarrowing
                -Wnoexcept
                -Wnon-template-friend
                -Wnon-virtual-dtor
                -Wnonnull
                -Woverflow
                -Woverlength-strings
                -Wparentheses
                -Wpmf-conversions
                -Wpointer-arith
                -Wreorder
                -Wreturn-type
                -Wsequence-point
                -Wsign-compare
                -Wswitch
                -Wtype-limits
                -Wundef
                -Wuninitialized
                -Wunused
                -Wvla
                -Wwrite-strings
                -Wno-redundant-move)
  endif ()
endif ()

# -- single output directory ---------------------------------------------------

# Use a single output directory for executable and library files.
# TODO These are deprecated; do we actually need this?
set(EXECUTABLE_OUTPUT_PATH
    ${CMAKE_CURRENT_BINARY_DIR}/bin
    CACHE PATH "Single directory for all executables")
set(LIBRARY_OUTPUT_PATH
    ${CMAKE_CURRENT_BINARY_DIR}/lib
    CACHE PATH "Single directory for all libraries")

# -- compiler setup ------------------------------------------------------------

set(CLANG_MINIMUM_VERSION 8.0)
set(APPLE_CLANG_MINIMUM_VERSION 9.1)
set(GCC_MINIMUM_VERSION 8.0)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  check_compiler_version(${GCC_MINIMUM_VERSION})
elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    check_compiler_version(${APPLE_CLANG_MINIMUM_VERSION})
  else ()
    check_compiler_version(${CLANG_MINIMUM_VERSION})
  endif ()
else ()
  message(WARNING "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
endif ()

# -- schemas -------------------------------------------------------------------

add_custom_target(
  vast-schema
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/schema"
    "${CMAKE_BINARY_DIR}/share/vast/schema/"
  COMMENT "Copying schema directory")

option(VAST_ENABLE_BUNDLED_SCHEMAS "Install bundled schemas with VAST" ON)
add_feature_info("VAST_ENABLE_BUNDLED_SCHEMAS" VAST_ENABLE_BUNDLED_SCHEMAS
                 "install bundled schemas with VAST.")
if (VAST_ENABLE_BUNDLED_SCHEMAS)
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/schema"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/vast")
endif ()

if (NOT VAST_DATADIR)
  set(VAST_DATADIR "${CMAKE_INSTALL_DATADIR}")
endif ()

if (NOT VAST_SYSCONFDIR)
  set(VAST_SYSCONFDIR "${CMAKE_INSTALL_SYSCONFDIR}")
endif ()

if (NOT VAST_LIBDIR)
  set(VAST_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
endif ()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/vast.yaml.example"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/vast/")

# -- documentation headers -----------------------------------------------------

# TODO: Move this to libvast/CMakeLists.txt, somehow.

file(
  GLOB doc_files CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "doc/cli/*.md")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/BANNER" doc_header NEWLINE_CONSUME)
set(doc_header "${doc_header}\n#pragma once\n")
set(doc_header "${doc_header}\nnamespace vast::documentation {\n")
foreach (x ${doc_files})
  get_filename_component(basename ${x} NAME_WE)
  string(REGEX REPLACE "-" "_" variable_name ${basename})
  string(TOUPPER ${variable_name} cmake_variable_name)
  file(STRINGS ${x} content NEWLINE_CONSUME)
  set(doc_statement "constexpr auto ${variable_name} = R\"__(${content})__\"\;")
  set(doc_header "${doc_header}\n${doc_statement}\n")
endforeach ()
set(doc_header "${doc_header}\n} // namespace vast::documentation")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/libvast/vast/documentation.hpp
     ${doc_header})

# -- add subdirectories --------------------------------------------------------

add_subdirectory(libvast)
add_subdirectory(libvast_test)
add_subdirectory(tools)
add_subdirectory(doc)
add_subdirectory(vast)

# -- cmake export/config installations -----------------------------------------

export(
  EXPORT VASTTargets
  FILE VASTTargets.cmake
  NAMESPACE vast::)

install(
  EXPORT VASTTargets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vast"
  NAMESPACE vast::)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/VASTConfigVersion.cmake"
  VERSION "${VAST_VERSION_MAJOR}.${VAST_VERSION_MINOR}.${VAST_VERSION_PATCH}"
  COMPATIBILITY ExactVersion)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VASTConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/VASTConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vast")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VASTConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/VASTConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vast")

# -- scripts -------------------------------------------------------------------

# TODO: Make other scripts installable as well.

option(VAST_ENABLE_CORELIGHT_CAT "Install corelight-cat with VAST" OFF)
add_feature_info("VAST_ENABLE_CORELIGHT_CAT" VAST_ENABLE_CORELIGHT_CAT
                 "install corelight-cat with VAST.")
if (VAST_ENABLE_CORELIGHT_CAT)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/corelight-cat"
          DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

# -- docdir --------------------------------------------------------------------

# Install LICENSE and README. This can intentionally not be disabled.
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.3rdparty"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}")

# -- feature summary -----------------------------------------------------------

# Append the feature summary to summary.log.
feature_summary(
  WHAT ALL
  FILENAME ${CMAKE_CURRENT_BINARY_DIR}/summary.log
  APPEND)

# Print the feature and build summary if we're not a subproject.
if (NOT VAST_IS_SUBPROJECT)
  message("\n")
  feature_summary(WHAT ENABLED_FEATURES DISABLED_FEATURES
                  INCLUDE_QUIET_PACKAGES)
  message(STATUS "Build summary:\n")
  message(" * Version: ${VAST_VERSION_TAG}")
  message(" * Build Tree Hash: ${VAST_BUILD_TREE_HASH}")
  message("")
  message(" * Build Type: ${CMAKE_BUILD_TYPE}")
  message(" * Source Directory: ${CMAKE_SOURCE_DIR}")
  message(" * Binary Directory: ${CMAKE_BINARY_DIR}\n")
  foreach (lang IN ITEMS C CXX)
    set(_lang_compiler "${CMAKE_${lang}_COMPILER}")
    set(_lang_compiler_info
        "${CMAKE_${lang}_COMPILER_ID} ${CMAKE_${lang}_COMPILER_VERSION}")
    set(_lang_flags
        "${CMAKE_${lang}_FLAGS} ${CMAKE_${lang}_FLAGS_${CMAKE_BUILD_TYPE}}
      ${CMAKE_CPP_FLAGS} ${CMAKE_CPP_FLAGS_${CMAKE_BUILD_TYPE}}")
    string(STRIP "${_lang_flags}" _lang_flags)
    if (_lang_flags)
      message(
        " * ${lang} Compiler: ${_lang_compiler} (${_lang_compiler_info} with ${_lang_flags})"
      )
    else ()
      message(" * ${lang} Compiler: ${_lang_compiler} (${_lang_compiler_info})")
    endif ()
  endforeach ()
  message(" * Linker: ${CMAKE_LINKER}")
  message(" * Archiver: ${CMAKE_AR}")
  get_property(VAST_DEPENDENCY_SUMMARY GLOBAL
               PROPERTY VAST_DEPENDENCY_SUMMARY_PROPERTY)
  if (VAST_DEPENDENCY_SUMMARY)
    message("")
    foreach (dependency_summary_entry IN LISTS VAST_DEPENDENCY_SUMMARY)
      message("${dependency_summary_entry}")
    endforeach ()
  endif ()
  message("")
endif ()
