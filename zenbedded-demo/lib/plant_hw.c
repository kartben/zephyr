/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Hardware plant backend: real PWM motor plus timer/SPI quadrature encoder,
 * driven from the "motor" and "encoder" phandles on each joint node.
 *
 * Phase 4. Deliberately not stubbed out with something that silently returns
 * zero: a control loop that thinks it is reading an encoder while reading a
 * constant is the single most dangerous thing this codebase could ship.
 */

#error "CONFIG_ZENBEDDED_PLANT_HW is Phase 4 and not implemented yet; use CONFIG_ZENBEDDED_PLANT_SIM"
