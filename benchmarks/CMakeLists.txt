add_executable(kalshi_benchmarks benchmarks.cpp)
target_include_directories(kalshi_benchmarks PRIVATE ${PROJECT_SOURCE_DIR}/src/api
    ${PROJECT_SOURCE_DIR}/src/ws)
target_link_libraries(kalshi_benchmarks PRIVATE
    kalshi::kalshi glaze::glaze OpenSSL::Crypto benchmark::benchmark_main)
kalshi_configure_target(kalshi_benchmarks)
if(TARGET kalshi_allocation_counter)
    target_link_libraries(kalshi_benchmarks PRIVATE kalshi_allocation_counter)
endif()
