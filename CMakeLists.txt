file(
    GLOB source_files
    RELATIVE src
    LIST_DIRECTORIES false
    "*.c"
    "*.cpp"
)

message("------------***************************-----------------")
message("files:" ${source_files})
message("------------***************************-----------------")

idf_component_register(
    SRCS
        ${srcs}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src/include"
)