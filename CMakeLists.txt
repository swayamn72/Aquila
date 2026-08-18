cmake_minimum_required(VERSION 3.25)
project(AquilaEngine VERSION 2.0 LANGUAGES CXX)

# ── C++23 required (concepts, std::pmr, starts_with, etc.) ──
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ─────────────────────────────────────────────
if(MSVC)
    # NOMINMAX: prevent Windows.h min/max macros
    # WIN32_LEAN_AND_MEAN: exclude rarely-used Windows headers
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    # NOTE: With MSVC+Visual Studio generator, multiple flags must be
    # listed as SEPARATE add_compile_options() calls inside the config
    # guard, NOT as a single space-separated string inside one <>.
    add_compile_options(/W4)
    # Always use /MP (multi-processor compilation) for faster builds
    add_compile_options(/MP)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Release: full optimization + native ISA (enables AVX2/AVX-512 if available)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    add_compile_options($<$<CONFIG:Release>:-flto>)
    add_compile_options($<$<CONFIG:Release>:-march=native>)
    add_compile_options($<$<CONFIG:Release>:-fno-rtti>)
    # Benchmark: release opt + debug symbols for perf profiling
    add_compile_options($<$<CONFIG:RelWithDebInfo>:-O3>)
    add_compile_options($<$<CONFIG:RelWithDebInfo>:-march=native>)
    add_compile_options($<$<CONFIG:Debug>:-Og>)
    add_compile_options($<$<CONFIG:Debug>:-g>)
    add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# ── Engine static library (shared by all executables) ──────────
set(ENGINE_SOURCES
    src/CSVReader.cpp
    src/SMAStrategy.cpp
    src/BollingerStrategy.cpp
    src/Portfolio.cpp
    src/ExecutionSimulator.cpp
    src/PerformanceAnalytics.cpp
    src/ReportExporter.cpp
    src/Engine.cpp       # intentionally empty — Engine<T> is header-only
)

add_library(aquila_lib STATIC ${ENGINE_SOURCES})
target_include_directories(aquila_lib PUBLIC src)
target_compile_features(aquila_lib PUBLIC cxx_std_23)

# ── Main executable ────────────────────────────────────────────
add_executable(AquilaEngine src/main.cpp)
target_link_libraries(AquilaEngine PRIVATE aquila_lib)

# ── Micro-benchmark suite ──────────────────────────────────────
add_executable(aquila_bench bench/bench_main.cpp)
target_include_directories(aquila_bench PRIVATE src)
target_compile_features(aquila_bench PRIVATE cxx_std_23)
if(MSVC)
    # /wd4834: suppress [[nodiscard]] warnings (intentional in bench)
    # /wd4100: suppress unreferenced formal parameter
    target_compile_options(aquila_bench PRIVATE /wd4834 /wd4100)
endif()

# ── GBM data generator ─────────────────────────────────────────
add_executable(gen_data tools/gen_data.cpp)
target_compile_features(gen_data PRIVATE cxx_std_23)
if(NOT MSVC)
    target_link_libraries(gen_data PRIVATE m)
endif()

# ── Installation ───────────────────────────────────────────────
install(TARGETS AquilaEngine aquila_bench gen_data
        RUNTIME DESTINATION bin)
