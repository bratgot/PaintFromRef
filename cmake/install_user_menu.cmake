# Post-install fixups at the install prefix (intended: %USERPROFILE%/.nuke or
# ~/.nuke). See NDK_NOTES 8.2/8.4.
#
# 1. Registers the plugin folder in init.py (runs in ALL modes, so the
#    version-dispatching PaintFromRef/init.py works headless and on farms).
# 2. Removes the registration block older installers put in menu.py (the
#    folder's own menu.py handles the toolbar once the path is registered).
# 3. Removes the flat-layout DLL from pre-multi-version installs, which
#    would otherwise shadow the per-version subfolders.

set(_marker_start "# --- PaintFromRef (auto-added by installer) ---")
set(_marker_end "# --- end PaintFromRef ---")
set(_block "\n${_marker_start}\nimport nuke\nnuke.pluginAddPath('./PaintFromRef')\n${_marker_end}\n")

# 1. ensure the block exists in init.py
set(_init "${CMAKE_INSTALL_PREFIX}/init.py")
if(EXISTS "${_init}")
    file(READ "${_init}" _content)
    string(FIND "${_content}" "${_marker_start}" _pos)
    if(_pos EQUAL -1)
        file(APPEND "${_init}" "${_block}")
        message(STATUS "PaintFromRef: appended registration to ${_init}")
    else()
        message(STATUS "PaintFromRef: already registered in ${_init}")
    endif()
else()
    file(WRITE "${_init}" "${_block}")
    message(STATUS "PaintFromRef: created ${_init} with registration")
endif()

# 2. drop the legacy block from menu.py if an older installer added it
set(_menu "${CMAKE_INSTALL_PREFIX}/menu.py")
if(EXISTS "${_menu}")
    file(READ "${_menu}" _mcontent)
    string(FIND "${_mcontent}" "${_marker_start}" _mstart)
    if(NOT _mstart EQUAL -1)
        string(FIND "${_mcontent}" "${_marker_end}" _mend)
        if(NOT _mend EQUAL -1)
            string(LENGTH "${_marker_end}" _mendlen)
            math(EXPR _mend "${_mend} + ${_mendlen}")
            string(SUBSTRING "${_mcontent}" 0 ${_mstart} _before)
            string(SUBSTRING "${_mcontent}" ${_mend} -1 _after)
            file(WRITE "${_menu}" "${_before}${_after}")
            message(STATUS "PaintFromRef: moved registration out of ${_menu}")
        endif()
    endif()
endif()

# 3. remove the flat-layout binary from pre-1.1 installs
foreach(_stale PaintFromRef.dll PaintFromRef.so)
    set(_p "${CMAKE_INSTALL_PREFIX}/PaintFromRef/${_stale}")
    if(EXISTS "${_p}")
        file(REMOVE "${_p}")
        message(STATUS "PaintFromRef: removed stale flat-layout ${_p}")
    endif()
endforeach()
