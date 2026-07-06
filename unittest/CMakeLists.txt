set(MODULE_NAME AYAnimation_UnitTests)

set(SRC_FILES
    main.cpp
    AYTest_KeySampler.cpp
    AYTest_AnimationPlayer.cpp
)

add_executable(${MODULE_NAME} ${SRC_FILES})

target_link_libraries(${MODULE_NAME} PRIVATE AYAnimation AYTest)
target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_test(NAME ${MODULE_NAME} COMMAND ${MODULE_NAME})