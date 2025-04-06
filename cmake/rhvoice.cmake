set(rhvoice_source_url "https://github.com/RHVoice/RHVoice.git")
set(rhvoice_tag "3e61d440308cedc53f56d0d9f22c380af80d933e")

find_package(Boost)
if(NOT ${Boost_FOUND})
   message(FATAL_ERROR "Boost not found but it is required by RHVoice")
endif()


ExternalProject_Add(rhvoice
    SOURCE_DIR ${external_dir}/rhvoice
    BINARY_DIR ${PROJECT_BINARY_DIR}/external/rhvoice
    INSTALL_DIR ${PROJECT_BINARY_DIR}/external
    GIT_REPOSITORY "${rhvoice_source_url}"
    GIT_TAG ${rhvoice_tag}
    GIT_SHALLOW OFF
    GIT_SUBMODULES "cmake/thirdParty/sanitizers" "cmake/thirdParty/CCache"
        "data/languages/Polish" "data/languages/Albanian" "data/languages/Brazilian-Portuguese"
        "data/languages/English" "data/languages/Esperanto" "data/languages/Georgian"
        "data/languages/Kyrgyz" "data/languages/Macedonian" "data/languages/Russian"
        "data/languages/Tatar" "data/languages/Ukrainian" "data/languages/Czech"
        "data/languages/Uzbek" "data/languages/Slovak" "data/languages/Serbian"
        "data/languages/Croatian" "data/languages/Spanish"
    UPDATE_COMMAND ""
    PATCH_COMMAND patch --batch --unified -p1 --directory=<SOURCE_DIR>
                -i ${patches_dir}/rhvoice.patch ||
                    echo "patch cmd failed, likely already patched"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_INSTALL_LIBDIR=${external_lib_dir}
        -DCMAKE_INSTALL_INCLUDEDIR=${external_include_dir} -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_PREFIX_PATH=<INSTALL_DIR> -DWITH_DATA=OFF -DENABLE_MAGE=OFF -DBUILD_CLIENT=OFF
        -DBUILD_UTILS=OFF -DBUILD_TESTS=OFF -DBUILD_SERVICE=OFF -DBUILD_SPEECHDISPATCHER_MODULE=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    BUILD_ALWAYS False
)

list(APPEND deps_libs "${external_lib_dir}/libRHVoice_core.so.1" "${external_lib_dir}/libRHVoice.so.1")
list(APPEND deps rhvoice)
