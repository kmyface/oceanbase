/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */
#ifndef CHARSET_ASCII_TAB_H_
#define CHARSET_ASCII_TAB_H_

#include "lib/charset/ob_ctype.h"
extern unsigned char ctype_ascii_general_ci[];
extern unsigned char to_lower_ascii_general_ci[];
extern unsigned char to_upper_ascii_general_ci[];
extern unsigned char sort_order_ascii_general_ci[];
extern uint16 to_uni_ascii_general_ci[];
extern unsigned char ctype_ascii_bin[];
extern unsigned char to_lower_ascii_bin[];
extern unsigned char to_upper_ascii_bin[];
extern uint16 to_uni_ascii_bin[];


#endif  // CHARSET_ASCII_TAB_H_
