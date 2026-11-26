# Sonuçları alan konsol istemcisi. İstemci sınıfı bir kütüphanede, böylece testler de kullanır.
add_library(inspector_receiver STATIC
    ReceiverClient.h
    ReceiverClient.cpp
    MessageFormat.h)
add_library(inspector::receiver ALIAS inspector_receiver)
set_target_properties(inspector_receiver PROPERTIES AUTOMOC ON)
target_include_directories(inspector_receiver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inspector_receiver PUBLIC inspector::net)
inspector_set_warnings(inspector_receiver)

add_executable(defect_receiver main.cpp)
target_link_libraries(defect_receiver PRIVATE inspector::receiver)
inspector_set_warnings(defect_receiver)
