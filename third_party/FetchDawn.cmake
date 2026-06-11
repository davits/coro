# Download a prebuilt Dawn release into <CMAKE_CURRENT_LIST_DIR>/dawn.
#
# The version is hardcoded; to upgrade, pick a release from
# https://github.com/google/dawn/releases and update the two variables below
# (the sha is part of the asset file names). The next cmake run re-downloads.

set(_dawn_version "v20260423.175430")
set(_dawn_sha "31e25af254ab572c77054edec4946d2244e184dd")

if(_FetchDawn_done)
    return()
endif()
set(_FetchDawn_done TRUE)

set(_dawn_root "${CMAKE_CURRENT_LIST_DIR}/dawn")
set(_dawn_stamp "${_dawn_root}/.fetchdawn-version")

set(_dawn_installed "")
if(EXISTS "${_dawn_stamp}")
    file(READ "${_dawn_stamp}" _dawn_installed)
endif()

if(NOT _dawn_installed STREQUAL "${_dawn_version}")
    if(APPLE)
        set(_dawn_platform "macos-latest")
    elseif(UNIX)
        set(_dawn_platform "ubuntu-latest")
    else()
        message(FATAL_ERROR "FetchDawn: unsupported host platform.")
    endif()

    set(_asset_name "Dawn-${_dawn_sha}-${_dawn_platform}-Release.tar.gz")
    set(_asset_url "https://github.com/google/dawn/releases/download/${_dawn_version}/${_asset_name}")
    set(_tarball "${CMAKE_CURRENT_BINARY_DIR}/${_asset_name}")

    message(STATUS "FetchDawn: downloading ${_asset_name} (${_dawn_version})")
    file(DOWNLOAD "${_asset_url}" "${_tarball}"
         STATUS _dl_status
         SHOW_PROGRESS
         TLS_VERIFY ON)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        file(REMOVE "${_tarball}")
        message(FATAL_ERROR "FetchDawn: download of ${_asset_url} failed: ${_dl_status}")
    endif()

    set(_extract_dir "${CMAKE_CURRENT_BINARY_DIR}/dawn_extract")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${_extract_dir}")
    file(REMOVE "${_tarball}")

    # Tarball layout: Dawn-<sha>-<platform>-Release/{include,lib,bin}/...
    # Flatten that single top-level directory into ${_dawn_root}.
    file(REMOVE_RECURSE "${_dawn_root}")
    file(RENAME "${_extract_dir}/Dawn-${_dawn_sha}-${_dawn_platform}-Release" "${_dawn_root}")
    file(REMOVE_RECURSE "${_extract_dir}")

    file(WRITE "${_dawn_stamp}" "${_dawn_version}")
    message(STATUS "FetchDawn: installed Dawn ${_dawn_version} into ${_dawn_root}")
endif()

# The ubuntu tarball installs the cmake package under lib64/, the others under lib/.
if(EXISTS "${_dawn_root}/lib64/cmake/Dawn")
    set(Dawn_DIR "${_dawn_root}/lib64/cmake/Dawn" CACHE PATH "Dawn cmake package dir" FORCE)
else()
    set(Dawn_DIR "${_dawn_root}/lib/cmake/Dawn" CACHE PATH "Dawn cmake package dir" FORCE)
endif()
find_package(Threads REQUIRED)
find_package(Dawn REQUIRED CONFIG)
