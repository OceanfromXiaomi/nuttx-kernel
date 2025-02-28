/****************************************************************************
 * arch/z80/src/z8/z80_mem.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __ARCH_Z80_SRC_Z8_Z80_MEM_H
#define __ARCH_Z80_SRC_Z8_Z80_MEM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* For the ZiLOG ZDS-II toolchain(s), the heap will be set using linker-
 * defined values:
 *
 *   far_heapbot  : set to the offset to the first unused value in EDATA
 *   far_stacktop : set to the highest address in EDATA
 *
 * The top of the heap is then determined by the amount of stack setaside
 * in the NuttX configuration file
 */

#ifndef CONFIG_HEAP1_BASE
extern far unsigned long far_heapbot;
#  define CONFIG_HEAP1_BASE ((uint16_t)&far_heapbot)
#endif

#ifndef CONFIG_HEAP1_END
extern far unsigned long far_stacktop;
#  define CONFIG_HEAP1_END \
     (((uint16_t)&far_stacktop) - CONFIG_IDLETHREAD_STACKSIZE)
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__
#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif

#endif /* __ARCH_Z80_SRC_Z8_Z80_MEM_H */
