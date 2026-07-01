if(CONFIG_ZCYPHAL)
  zephyr_library()
  zephyr_library_sources(
    ${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy/cy.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy_can/cy_can.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/deps/libcanard/libcanard/canard.c
    src/cy_can_zephyr.c
    src/identity.c
    src/heap.c
    src/zcyphal.c
    src/zcyphal_log.c
  )
  zephyr_include_directories(include)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/lib)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy_can)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/libcanard/libcanard)
  zephyr_library_compile_definitions_ifdef(CONFIG_ZCYPHAL_TRACE CY_CONFIG_TRACE=1)
  # wild_key_value (in cy.c) miscompiles under -Os on GCC for multi-segment topic names.
  set_source_files_properties(
    ${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy/cy.c
    PROPERTIES COMPILE_OPTIONS -Og
  )
endif()
