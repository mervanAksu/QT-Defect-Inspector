# Ağ katmanı: sonuçları JSON Lines olarak TCP/UDP üzerinden yayınlar.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(inspector_net STATIC
    include/inspector/Protocol.h
    include/inspector/LineBuffer.h
    include/inspector/ResultPublisher.h
    src/Protocol.cpp
    src/LineBuffer.cpp
    src/ResultPublisher.cpp)
add_library(inspector::net ALIAS inspector_net)
set_target_properties(inspector_net PROPERTIES AUTOMOC ON)

target_include_directories(inspector_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(inspector_net PUBLIC inspector::pipeline Qt6::Network)
inspector_set_warnings(inspector_net)
