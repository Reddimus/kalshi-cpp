# Example: Basic API usage
add_executable(example_basic basic_usage.cpp)
target_link_libraries(example_basic PRIVATE kalshi)

# Example: Get markets
add_executable(example_markets get_markets.cpp)
target_link_libraries(example_markets PRIVATE kalshi)

# Example: Get daily temperature markets (high and low)
add_executable(example_daily_temp get_daily_temp.cpp)
target_link_libraries(example_daily_temp PRIVATE kalshi)
