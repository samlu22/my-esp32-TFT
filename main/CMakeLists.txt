idf_component_register(
    SRCS
        "main.c"
        "pureflow.c"
    INCLUDE_DIRS "."
)

target_link_options(${COMPONENT_LIB} INTERFACE -Wl,--whole-archive)