# Try to find libtsm
# Once done this will define
#
#  LIBTSM_FOUND - system has libtsm
#  LIBTSM_LIBRARIES - Link these to use libtsm

if(LIBTSM_LIBRARIES)
    set (LIBTSM_FIND_QUIETLY TRUE)
endif()

FIND_PATH(LIBTSM_INCLUDE_DIRS libtsm.h)
FIND_LIBRARY(LIBTSM_LIBRARIES NAMES tsm)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBTSM DEFAULT_MSG LIBTSM_LIBRARIES LIBTSM_INCLUDE_DIRS)

if (LIBTSM_FOUND)
    message (STATUS "Found components for libtsm")
    message (STATUS "LIBTSM_LIBRARIES = ${LIBTSM_LIBRARIES}")
else (LIBTSM_FOUND)
    if (LIBTSM_FIND_REQUIRED)
        message (FATAL_ERROR "Could not find libtsm!")
    endif (LIBTSM_FIND_REQUIRED)
endif (LIBTSM_FOUND)

MARK_AS_ADVANCED(LIBTSM_LIBRARIES LIBTSM_INCLUDE_DIRS)
