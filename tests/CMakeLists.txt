# ── Jellyfin backend unit tests ──────────────────────────────────────────────────
#
# Build:   cmake -B build -DBUILD_TESTS=ON . && cmake --build build
# Run:     ./build/test_jellyfin
# Run all: ctest --test-dir build -V

if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(test_jellyfin
        jellyfin/JellyfinBackendTest.cpp
        TestHelpers.cpp
        mocks/FakeNetworkReply.cpp
        mocks/MockNetworkAccessManager.cpp
        ${CMAKE_SOURCE_DIR}/src/modules/jellyfin/JellyfinBackend.cpp
    )

    target_compile_definitions(test_jellyfin PRIVATE UNIT_TEST)

    target_link_libraries(test_jellyfin PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Network
    )

    target_include_directories(test_jellyfin PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME JellyfinBackend COMMAND test_jellyfin)

    # ── Manifest schema validation ──────────────────────────────────────────
    add_executable(test_jellyfin_manifest
        jellyfin/ManifestTest.cpp
    )
    target_link_libraries(test_jellyfin_manifest PRIVATE
        Qt6::Test
        Qt6::Core
    )
    target_include_directories(test_jellyfin_manifest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME JellyfinManifest COMMAND test_jellyfin_manifest)
endif()
