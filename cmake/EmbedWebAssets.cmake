# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles the contents of a directory into a C++ asset table.
#
# The device has no filesystem we want to depend on for its own UI: the config
# page has to work on a device whose storage is questionable, which is exactly
# when someone needs it most. So the pages live in the binary.
#
# Assets are embedded as raw string literals rather than byte arrays. That keeps
# the generated file readable when something goes wrong, compiles far faster
# than a few hundred thousand comma-separated integers, and needs no tool beyond
# CMake itself — no Python in the build, which matters for the pinned Docker
# cross-toolchain in Phase 7.
#
# The cost of that choice is that assets must be text. Binary files would need
# hex encoding and a different generator; if a favicon is ever wanted, inline
# SVG in the HTML avoids the question entirely.

# Delimiter for the raw string literals. Chosen to be something no web source
# would plausibly contain; verified per file below rather than assumed.
set(NOTRIX_WEB_DELIMITER "NOTRIXWEB")

function(_notrix_web_content_type path out_var)
    get_filename_component(extension "${path}" EXT)
    string(TOLOWER "${extension}" extension)

    if(extension STREQUAL ".html")
        set(${out_var} "text/html; charset=utf-8" PARENT_SCOPE)
    elseif(extension STREQUAL ".css")
        set(${out_var} "text/css; charset=utf-8" PARENT_SCOPE)
    elseif(extension STREQUAL ".js")
        set(${out_var} "application/javascript; charset=utf-8" PARENT_SCOPE)
    elseif(extension STREQUAL ".json")
        set(${out_var} "application/json" PARENT_SCOPE)
    elseif(extension STREQUAL ".svg")
        set(${out_var} "image/svg+xml" PARENT_SCOPE)
    else()
        # Refused rather than guessed. Serving the wrong content type produces
        # a page that fails in the browser with no clue why, and a build error
        # here is much cheaper to understand than that is.
        message(FATAL_ERROR
            "notrix_embed_web_assets: no content type known for '${path}'. "
            "Add one to _notrix_web_content_type, or reconsider shipping this "
            "file in the firmware.")
    endif()
endfunction()

# notrix_embed_web_assets(<source-dir> <output-cpp> <out-var-for-dependencies>)
function(notrix_embed_web_assets source_dir output_file deps_var)
    file(GLOB_RECURSE asset_files RELATIVE "${source_dir}" "${source_dir}/*")
    list(SORT asset_files)

    if(NOT asset_files)
        message(FATAL_ERROR
            "notrix_embed_web_assets: no files found under '${source_dir}'. "
            "The device UI would silently 404 on every path.")
    endif()

    set(generated "// SPDX-License-Identifier: GPL-3.0-or-later\n")
    string(APPEND generated "//\n")
    string(APPEND generated "// GENERATED FILE - do not edit.\n")
    string(APPEND generated "// Written by cmake/EmbedWebAssets.cmake from firmware/web/.\n")
    string(APPEND generated "// Edit the sources there and rebuild.\n")
    string(APPEND generated "\n#include \"notrix/web/WebAssets.h\"\n")
    string(APPEND generated "\nnamespace notrix {\nnamespace web {\nnamespace {\n\n")

    set(table "")
    set(index 0)
    set(dependencies "")

    foreach(relative IN LISTS asset_files)
        set(absolute "${source_dir}/${relative}")
        list(APPEND dependencies "${absolute}")

        _notrix_web_content_type("${relative}" content_type)
        file(READ "${absolute}" contents)
        file(MD5 "${absolute}" digest)

        # A file containing the closing delimiter would end its own literal and
        # spray the rest of itself into the generated source as code.
        string(FIND "${contents}" ")${NOTRIX_WEB_DELIMITER}\"" clash)
        if(NOT clash EQUAL -1)
            message(FATAL_ERROR
                "notrix_embed_web_assets: '${relative}' contains the raw string "
                "delimiter )${NOTRIX_WEB_DELIMITER}\". Change NOTRIX_WEB_DELIMITER "
                "in cmake/EmbedWebAssets.cmake.")
        endif()

        string(APPEND generated
            "constexpr std::string_view kBody${index} =\n"
            "    R\"${NOTRIX_WEB_DELIMITER}(${contents})${NOTRIX_WEB_DELIMITER}\";\n\n")

        string(APPEND table
            "    Asset{\"/${relative}\", \"${content_type}\", kBody${index}, \"\\\"${digest}\\\"\"},\n")

        math(EXPR index "${index} + 1")
    endforeach()

    string(APPEND generated "constexpr Asset kAssets[] = {\n${table}};\n\n")
    string(APPEND generated "}  // namespace\n\n")
    string(APPEND generated "const Asset* assets() noexcept { return kAssets; }\n\n")
    string(APPEND generated
        "int assetCount() noexcept {\n"
        "    return static_cast<int>(sizeof(kAssets) / sizeof(kAssets[0]));\n"
        "}\n\n")
    string(APPEND generated
        "const Asset* findAsset(std::string_view path) noexcept {\n"
        "    for (const Asset& asset : kAssets) {\n"
        "        if (asset.path == path) {\n"
        "            return &asset;\n"
        "        }\n"
        "    }\n"
        "    return nullptr;\n"
        "}\n\n")
    string(APPEND generated "}  // namespace web\n}  // namespace notrix\n")

    # Only rewrite when the content actually differs, so an unchanged UI does
    # not force a rebuild of everything downstream of it.
    set(existing "")
    if(EXISTS "${output_file}")
        file(READ "${output_file}" existing)
    endif()
    if(NOT existing STREQUAL generated)
        file(WRITE "${output_file}" "${generated}")
    endif()

    set(${deps_var} "${dependencies}" PARENT_SCOPE)
endfunction()
