set(API_PATH ${CMAKE_SOURCE_DIR}/api)
include_directories(${API_PATH})
set(SOURCES staticfile-test.cpp)
CREATE_UNIT_TEST(staticfile-test Core: "${SOURCES}")
target_link_libraries(staticfile-test Core::Lib)
add_dependencies(staticfile-test static_file)