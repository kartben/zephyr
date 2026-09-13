/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>

/*
 * The devicetree gives AXISRAM1 the whole two megabyte block, but the image
 * itself is loaded partway into it, and the two are separate linker regions so
 * nothing diagnoses the collision. Stop a build whose buffers would land on
 * the image rather than letting it corrupt itself at run time.
 */
ASSERT(__AXISRAM1_end <= DT_REG_ADDR(DT_NODELABEL(axisram2)),
       "AXISRAM1 buffers reach into the image: shrink them or move them out")
