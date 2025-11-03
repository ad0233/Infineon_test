idf_component_register(INCLUDE_DIRS ".")
target_link_libraries(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libmy_fsm.a")