# Example: Basic API usage
add_executable(example_basic basic_usage.cpp)
target_link_libraries(example_basic PRIVATE kalshi)

# Example: Get markets
add_executable(example_markets get_markets.cpp)
target_link_libraries(example_markets PRIVATE kalshi)

# Example: Get daily high temperature markets
add_executable(example_daily_high_temp get_daily_high_temp.cpp)
target_link_libraries(example_daily_high_temp PRIVATE kalshi)
