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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_unistr.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/charset/ob_charset_string_helper.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
ObExprUnistr::ObExprUnistr(common::ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_UNISTR, N_UNISTR, 1, VALID_FOR_GENERATED_COL)
{
}
ObExprUnistr::~ObExprUnistr()
{
}

int ObExprUnistr::calc_result_type1(ObExprResType &type,
                                    ObExprResType &type1,
                                    common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObLength length = 0;
  type.set_nvarchar2();
  type.set_collation_type(type_ctx.get_session()->get_nls_collation_nation());
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_length_semantics(LS_CHAR);
  if (!type1.is_character_type()) {
    type1.set_calc_type(ObVarcharType);
    type1.set_calc_collation_type(type_ctx.get_session()->get_dtc_params().nls_collation_);
    type1.set_calc_length_semantics(LS_CHAR);
  }

  OZ (ObExprResultTypeUtil::deduce_max_string_length_oracle(type_ctx.get_session()->get_dtc_params(),
                                                            type1,
                                                            type,
                                                            length));
  type1.set_calc_length(length);
  type.set_length(length);
  return ret;
}

int ObExprUnistr::calc_unistr(const common::ObString &src,
                const common::ObCollationType src_cs_type,
                const common::ObCollationType dst_cs_type,
                char* buf, const int64_t buf_len, int32_t &pos)
{
  int ret = OB_SUCCESS;
  struct Functor {
    Functor(char* buf, const int64_t buf_len, int32_t &pos, int64_t total_str_len, const common::ObCollationType dst_cs_type)
      : buf(buf), buf_len(buf_len), pos(pos), total_str_len(total_str_len), dst_cs_type(dst_cs_type) {}
    // for unicode inner scan loop
    // 以下四元组构成改造中的循环不变量, incomming_char_len 外层循环参数, unicode_inner_len 内部循环参数
    // (incomming_char_len, total_str_len, (unicode_inner_len, unicode_encoding_value), (buf, buf_len, pos))
    int incoming_char_len = 0;

    int found_backslash = false;
    int unicode_inner_len = 0;
    int64_t unicode_encoding_value = 0;

    char* buf;
    const int64_t buf_len;
    int32_t &pos;

    const int64_t total_str_len;
    const common::ObCollationType dst_cs_type;
    bool is_ucs2_format = false;

    int operator() (const ObString &str, ob_wc_t wchar) {
      int ret = OB_SUCCESS;
      incoming_char_len += str.length();
      if (!found_backslash) {
        if ('\\' != wchar) {
          int32_t written_bytes = 0;
          if (OB_FAIL(ObCharset::wc_mb(dst_cs_type, wchar,
                                      buf + pos, buf_len - pos, written_bytes))) {
            LOG_WARN("fail to convert unicode to multi-byte", K(ret), K(wchar));
          } else {
            pos += written_bytes;
          }
        } else {
          found_backslash = true;
          is_ucs2_format = true;
          unicode_inner_len = 0;
        }
      } else {
        if (unicode_inner_len == 0 && '\\' == wchar) {
          //found "\\"
          int32_t written_bytes = 0;
          if (OB_FAIL(ObCharset::wc_mb(dst_cs_type, wchar,
                                      buf + pos, buf_len - pos, written_bytes))) {
            LOG_WARN("fail to convert unicode to multi-byte", K(ret), K(wchar));
          } else {
            pos += written_bytes;
          }
          is_ucs2_format = false;
          found_backslash = false;
          unicode_inner_len = 0;
        } else if (unicode_inner_len < 4){
          int64_t value = 0;
          if ('0' <= wchar && wchar <= '9') {
            value = wchar - '0';
          } else if ('A' <= wchar && wchar <= 'F') {
            value = wchar - 'A' + 10;
          } else if ('a' <= wchar && wchar <= 'f') {
            value = wchar - 'a' + 10;
          } else {
            ret = OB_ERR_MUST_BE_FOLLOWED_BY_FOUR_HEXADECIMAL_CHARACTERS_OR_ANOTHER;
            LOG_WARN("fail to get next character", K(ret));
          }
          if (OB_SUCC(ret)) {
            unicode_encoding_value *= 16;
            unicode_encoding_value += value;
            ++unicode_inner_len;
          }
        }
        if (OB_SUCC(ret)) {
          if (is_ucs2_format && unicode_inner_len == 4) {
            if (OB_UNLIKELY(pos + 2 > buf_len)) {
              ret = OB_SIZE_OVERFLOW;
              LOG_WARN("size overflow", K(ret));
            } else {
              buf[pos++] = (unicode_encoding_value >> 8) & 0xFF;
              buf[pos++] = unicode_encoding_value & 0xFF;
              unicode_inner_len = 0;
              unicode_encoding_value = 0;
              found_backslash = false;
              is_ucs2_format = false;
            }
          }
        }
      }

      if (OB_SUCC(ret)) {
        // there may be no chance for total_str_len + 1
        if (incoming_char_len == total_str_len) {
          if (found_backslash) {
            ret = OB_ERR_MUST_BE_FOLLOWED_BY_FOUR_HEXADECIMAL_CHARACTERS_OR_ANOTHER;
            LOG_WARN("fail to get next character", K(ret));
          }
        }
      }
      return ret;
    }
  };

  Functor temp_handler(buf, buf_len, pos, src.length(), dst_cs_type);
  ObCharsetType src_charset_type = ObCharset::charset_type_by_coll(src_cs_type);
  OZ(ObFastStringScanner::foreach_char(src, src_charset_type, temp_handler));
  return ret;
}

int ObExprUnistr::calc_unistr_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *src_param = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, src_param))) {
    LOG_WARN("eval arg failed", K(ret));
  } else {
    if (src_param->is_null()) {
      res_datum.set_null();
    } else {
      ObString src = src_param->get_string();
      char *buf = NULL;
      int64_t buf_len = src.length() * ObCharset::MAX_MB_LEN;
      int32_t length = 0;

      if (OB_ISNULL(buf = static_cast<char*>(expr.get_str_res_mem(ctx, buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate memory", K(ret), K(src));
      } else if (OB_FAIL(calc_unistr(src, expr.args_[0]->datum_meta_.cs_type_,
                                     expr.datum_meta_.cs_type_,
                                     buf, buf_len, length))) {
         LOG_WARN("fail to calc unistr", K(ret));
      } else {
        res_datum.set_string(buf, length);
      }
    }
  }
  return ret;
}

int ObExprUnistr::cg_expr(ObExprCGCtx &op_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_unistr_expr;
  return ret;
}

ObExprAsciistr::ObExprAsciistr(common::ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_ASCIISTR, N_ASCIISTR, 1, VALID_FOR_GENERATED_COL)
{
}
ObExprAsciistr::~ObExprAsciistr()
{
}

int ObExprAsciistr::calc_result_type1(ObExprResType &type,
                                      ObExprResType &type1,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;

  //deduce result type
  type.set_varchar();
  type.set_collation_type(type_ctx.get_session()->get_nls_collation());
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  if (type1.is_character_type()
      && (type1.get_length_semantics() == LS_CHAR || type1.get_length_semantics() == LS_BYTE)) {
    type.set_length_semantics(type1.get_length_semantics());
  } else {
    type.set_length_semantics(type_ctx.get_session()->get_actual_nls_length_semantics());
  }

  //deduce calc type
  if (!type1.is_character_type()) {
    type1.set_calc_type(ObVarcharType);
    type1.set_calc_collation_type(type_ctx.get_session()->get_nls_collation());
  }
  type1.set_calc_length_semantics(type.get_length_semantics());
  
  //deduce length
  ObLength length = 0;
  ObExprResType temp_type;
  temp_type.set_meta(type1.get_calc_meta());
  temp_type.set_length_semantics(type.get_length_semantics());
  OZ (ObExprResultTypeUtil::deduce_max_string_length_oracle(type_ctx.get_session()->get_dtc_params(),
                                                            type1,
                                                            temp_type,
                                                            length));
  type1.set_calc_length(length);
  type.set_length(length * 10);
  return ret;
}

int calc_asciistr(const ObString &src,
                  const ObCollationType src_cs_type,
                  const ObCollationType dst_cs_type,
                  char* buf, const int64_t buf_len, int32_t &pos)
{
  int ret = OB_SUCCESS;
  struct Functor {
    Functor(char* buf, const int64_t buf_len, int32_t &pos, const common::ObCollationType dst_cs_type)
      : buf(buf), buf_len(buf_len), pos(pos), dst_cs_type(dst_cs_type) {}
    char* buf;
    const int64_t buf_len;
    int32_t &pos;
    const ObCollationType dst_cs_type;
    int operator() (const ObString &str, ob_wc_t wchar) {
      int ret = OB_SUCCESS;
      if (ob_isascii(wchar) && '\\' != wchar) {
        int32_t written_bytes = 0;

        if (OB_FAIL(ObCharset::wc_mb(dst_cs_type, wchar,
                                    buf + pos, buf_len - pos, written_bytes))) {
          LOG_WARN("fail to convert unicode to multi-byte", K(ret), K(wchar));
        } else {
          pos += written_bytes;
        }
      } else {
        const int64_t temp_buf_len = 4;
        char temp_buf[temp_buf_len];
        int32_t temp_written_bytes = 0;

        if (OB_FAIL(ObCharset::wc_mb(CS_TYPE_UTF16_BIN, wchar,
                                    temp_buf, temp_buf_len, temp_written_bytes))) {
          LOG_WARN("fail to convert unicode to multi-byte", K(ret), K(wchar));
        } else {
          const int utf16_minmb_len = 2;

          if (OB_UNLIKELY(ObCharset::is_cs_nonascii(dst_cs_type))) {
            // not support non-ascii database charset for now
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "charset except ascii");
            LOG_WARN("not support charset", K(ret), K(dst_cs_type));
            /*
            const int64_t hex_buf_len = temp_buf_len * 2;
            char hex_buf[hex_buf_len];
            int32_t hex_written_bytes = 0;

            for (int i = 0; OB_SUCC(ret) && i < temp_written_bytes/utf16_minmb_len; ++i) {
              if (OB_FAIL(ObCharset::wc_mb(dst_cs_type, '\\',
                                          buf + pos, buf_len - pos, written_bytes))) {
                LOG_WARN("fail to convert unicode to multi-byte", K(ret), K(wchar));
              } else {
                pos += written_bytes;
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(hex_print(temp_buf, utf16_minmb_len,
                                      hex_buf, hex_buf_len, hex_written_bytes))) {
                  LOG_WARN("fail to convert to hex", K(ret), K(temp_written_bytes), K(pos), K(buf_len));
                } else if (OB_FAIL(ObCharset::charset_convert(CS_TYPE_UTF8MB4_BIN,
                                                              hex_buf, hex_written_bytes,
                                                              dst_cs_type,
                                                              buf + pos, buf_len - pos, written_bytes))) {
                  LOG_WARN("fail to convert charset", K(ret));
                } else {
                  pos += written_bytes;
                }
              }
            }
            */
          } else {
            for (int i = 0; OB_SUCC(ret) && i < temp_written_bytes/utf16_minmb_len; ++i) {
              if (OB_UNLIKELY(pos >= buf_len)) {
                ret = OB_SIZE_OVERFLOW;
                LOG_WARN("size overflow", K(ret), K(pos), K(buf_len));
              } else {
                buf[pos++] = '\\';
              }
              if (OB_SUCC(ret)) {
                int64_t hex_writtern_bytes = 0;
                if (OB_FAIL(hex_print(temp_buf + i*utf16_minmb_len, utf16_minmb_len,
                                      buf + pos, buf_len - pos, hex_writtern_bytes))) {
                  LOG_WARN("fail to convert to hex", K(ret), K(temp_written_bytes), K(pos), K(buf_len));
                } else {
                  pos += hex_writtern_bytes;
                }
              }
            }
          }
        }
      }
      return ret;
    };
  };

  ObCharsetType src_charset_type = ObCharset::charset_type_by_coll(src_cs_type);
  Functor temp_handler(buf, buf_len, pos, dst_cs_type);
  OZ(ObFastStringScanner::foreach_char(src, src_charset_type, temp_handler));
  return ret;
}

int ObExprAsciistr::calc_asciistr_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *src_param = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, src_param))) {
    LOG_WARN("eval arg failed", K(ret));
  } else {
    if (src_param->is_null()) {
      res_datum.set_null();
    } else {
      ObString src = src_param->get_string();
      char *buf = NULL;
      int64_t buf_len = src.length() * ObCharset::MAX_MB_LEN * 2;
      int32_t length = 0;

      if (OB_ISNULL(buf = static_cast<char*>(expr.get_str_res_mem(ctx, buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate memory", K(ret), K(src));
      } else if (OB_FAIL(calc_asciistr(src, expr.args_[0]->datum_meta_.cs_type_,
                                       expr.datum_meta_.cs_type_,
                                       buf, buf_len, length))) {
         LOG_WARN("fail to calc unistr", K(ret));
      } else {
        res_datum.set_string(buf, length);
      }
    }
  }
  return ret;
}

int ObExprAsciistr::cg_expr(ObExprCGCtx &op_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_asciistr_expr;
  return ret;
}

}
}
