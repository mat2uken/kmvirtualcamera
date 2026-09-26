# Applies a git patch to a FetchContent source tree.
#
#     cmake -DREPO=<fetched source dir> -DPATCH=<patch file> \
#           -P apply_libdatachannel_patch.cmake
#
# ExternalProject re-runs the PATCH_COMMAND step on later configures, so a bare
# "git apply" fails the second time with "patch does not apply". The step is
# therefore made idempotent: if the patch does not apply as-is, reverse apply is
# checked (already applied -> nothing to do) and, failing that, the fetched tree
# is restored to the pinned commit before applying. Local edits under the
# fetched dependency are build artifacts and are not kept.

if(NOT DEFINED REPO OR NOT DEFINED PATCH)
    message(FATAL_ERROR "apply_libdatachannel_patch.cmake requires REPO and PATCH")
endif()
if(NOT EXISTS "${REPO}")
    message(FATAL_ERROR "fetched source directory does not exist: ${REPO}")
endif()
if(NOT EXISTS "${PATCH}")
    message(FATAL_ERROR "patch file does not exist: ${PATCH}")
endif()

function(check_patch result_var)
    execute_process(
        COMMAND git apply -p1 --check "${PATCH}"
        WORKING_DIRECTORY "${REPO}"
        RESULT_VARIABLE rc
        OUTPUT_QUIET
        ERROR_QUIET)
    set(${result_var} "${rc}" PARENT_SCOPE)
endfunction()

check_patch(rc)
if(NOT rc EQUAL 0)
    execute_process(
        COMMAND git apply -p1 -R --check "${PATCH}"
        WORKING_DIRECTORY "${REPO}"
        RESULT_VARIABLE reversed
        OUTPUT_QUIET
        ERROR_QUIET)
    if(reversed EQUAL 0)
        message(STATUS "libdatachannel patch already applied: ${PATCH}")
        return()
    endif()

    message(STATUS "Restoring ${REPO} to the pinned commit before patching")
    execute_process(
        COMMAND git checkout -- .
        WORKING_DIRECTORY "${REPO}"
        RESULT_VARIABLE restored)
    if(NOT restored EQUAL 0)
        message(FATAL_ERROR "git checkout -- . failed in ${REPO}")
    endif()
    # usrsctp is a nested clone of the same dependency; its working tree is
    # restored the same way.
    if(EXISTS "${REPO}/deps/usrsctp/.git")
        execute_process(
            COMMAND git checkout -- .
            WORKING_DIRECTORY "${REPO}/deps/usrsctp"
            RESULT_VARIABLE restored_usrsctp)
        if(NOT restored_usrsctp EQUAL 0)
            message(FATAL_ERROR "git checkout -- . failed in ${REPO}/deps/usrsctp")
        endif()
    endif()

    check_patch(rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "patch does not apply to ${REPO}: ${PATCH}")
    endif()
endif()

execute_process(
    COMMAND git apply -p1 "${PATCH}"
    WORKING_DIRECTORY "${REPO}"
    RESULT_VARIABLE applied)
if(NOT applied EQUAL 0)
    message(FATAL_ERROR "git apply failed: ${PATCH}")
endif()
message(STATUS "Applied libdatachannel patch: ${PATCH}")
