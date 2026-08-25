# Derives COMMIT_ID / COMMIT_TIME from git, falling back gracefully when
# building from a tree with no git history (e.g. a source tarball).
find_package(Git QUIET)

set(POMELOLIB_COMMIT_ID "unknown")
set(POMELOLIB_COMMIT_TIME "unknown")

if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE _git_commit_id
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_result
    ERROR_QUIET
  )
  if(_git_result EQUAL 0)
    set(POMELOLIB_COMMIT_ID ${_git_commit_id})
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} log --pretty=%ci -n1
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE _git_commit_time
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_result
    ERROR_QUIET
  )
  if(_git_result EQUAL 0)
    set(POMELOLIB_COMMIT_TIME ${_git_commit_time})
  endif()
endif()
