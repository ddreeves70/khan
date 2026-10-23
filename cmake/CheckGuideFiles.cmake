# cmake -DGUIDE=<file.md> -P CheckGuideFiles.cmake
#
# Checks that every code block marked with
#   <!-- guide-file: relative/path -->
# in a guide page is exactly the content of that file (path relative to the
# page). Keeps the tutorial's code from drifting away from the code the tests
# build and load.
cmake_minimum_required(VERSION 3.28)
file(READ "${GUIDE}" text)
get_filename_component(base "${GUIDE}" DIRECTORY)
set(marker "<!-- guide-file: ")
set(checked 0)
set(failed 0)
while(TRUE)
  string(FIND "${text}" "${marker}" at)
  if(at EQUAL -1)
    break()
  endif()
  string(LENGTH "${marker}" mlen)
  math(EXPR start "${at} + ${mlen}")
  string(SUBSTRING "${text}" ${start} -1 text)
  string(FIND "${text}" " -->" end)
  string(SUBSTRING "${text}" 0 ${end} rel)
  # The block starts on the line after the opening ``` fence.
  string(FIND "${text}" "```" fence)
  string(SUBSTRING "${text}" ${fence} -1 text)
  string(FIND "${text}" "\n" nl)
  math(EXPR nl "${nl} + 1")
  string(SUBSTRING "${text}" ${nl} -1 text)
  string(FIND "${text}" "```" close)
  string(SUBSTRING "${text}" 0 ${close} block)
  string(SUBSTRING "${text}" ${close} -1 text)

  file(READ "${base}/${rel}" actual)
  string(REPLACE "\r\n" "\n" actual "${actual}")
  string(REPLACE "\r\n" "\n" block "${block}")
  math(EXPR checked "${checked} + 1")
  if(NOT block STREQUAL actual)
    message(SEND_ERROR "${GUIDE}: the code block for ${rel} doesn't match ${base}/${rel}. "
                       "Copy the file's current content into the guide.")
    math(EXPR failed "${failed} + 1")
  endif()
endwhile()
if(checked EQUAL 0)
  message(FATAL_ERROR "${GUIDE}: no guide-file blocks found")
endif()
message(STATUS "${GUIDE}: ${checked} code blocks match their files")
