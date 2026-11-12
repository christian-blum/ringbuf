FILE(GLOB_RECURSE sources *.c *.cpp)

idf_component_register(
    SRCS
        ${sources}
    INCLUDE_DIRS
        "include"
)