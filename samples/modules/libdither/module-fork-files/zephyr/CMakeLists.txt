# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LIBDITHER)
  zephyr_library_named(libdither)

  set(LIBDITHER_SRC ${ZEPHYR_LIBDITHER_MODULE_DIR}/src/libdither)

  zephyr_include_directories(${LIBDITHER_SRC})

  zephyr_library_sources(
    ${LIBDITHER_SRC}/gamma.c
    ${LIBDITHER_SRC}/queue.c
    ${LIBDITHER_SRC}/random.c
    ${LIBDITHER_SRC}/ditherimage.c
    ${LIBDITHER_SRC}/libdither.c
    ${LIBDITHER_SRC}/color_bytecolor.c
    ${LIBDITHER_SRC}/color_bytepalette.c
    ${LIBDITHER_SRC}/color_cachedpalette.c
    ${LIBDITHER_SRC}/color_colorimage.c
    ${LIBDITHER_SRC}/color_floatcolor.c
    ${LIBDITHER_SRC}/color_floatpalette.c
    ${LIBDITHER_SRC}/color_models.c
    ${LIBDITHER_SRC}/color_quant_kdtree.c
    ${LIBDITHER_SRC}/color_quant_mediancut.c
    ${LIBDITHER_SRC}/color_quant_wu.c
    ${LIBDITHER_SRC}/dither_dbs.c
    ${LIBDITHER_SRC}/dither_dotdiff.c
    ${LIBDITHER_SRC}/dither_dotlippens.c
    ${LIBDITHER_SRC}/dither_errordiff.c
    ${LIBDITHER_SRC}/dither_grid.c
    ${LIBDITHER_SRC}/dither_kallebach.c
    ${LIBDITHER_SRC}/dither_ordered.c
    ${LIBDITHER_SRC}/dither_pattern.c
    ${LIBDITHER_SRC}/dither_riemersma.c
    ${LIBDITHER_SRC}/dither_threshold.c
    ${LIBDITHER_SRC}/dither_varerrdiff.c
    ${LIBDITHER_SRC}/kdtree/kdtree.c
    ${LIBDITHER_SRC}/tetrapal/tetrapal.c
  )
endif()
