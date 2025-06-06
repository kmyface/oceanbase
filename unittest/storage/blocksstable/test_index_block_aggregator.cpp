/**
 * Copyright (c) 2022 OceanBase
 * OceanBase is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX STORAGE

#include <gtest/gtest.h>
#define protected public
#define private public
#include "storage/blocksstable/index_block/ob_index_block_aggregator.h"
#include "storage/blocksstable/cs_encoding/ob_micro_block_cs_encoder.h"
#include "storage/blocksstable/encoding/ob_micro_block_encoder.h"
#include "ob_row_generate.h"


namespace oceanbase
{
namespace blocksstable
{
using namespace common;
using namespace storage;
using namespace share::schema;

class TestIndexBlockAggregator : public ::testing::Test
{
public:
  TestIndexBlockAggregator()
  {
    data_version_ = DATA_VERSION_4_3_5_2;
  }
  virtual ~TestIndexBlockAggregator() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
  void init_schema(const int64_t col_count, const ObObjType *col_obj_types);
  void init_data_encoder(const ObRowStoreType row_store_type, ObIMicroBlockWriter *&micro_writer);
  void init_min_max_meta(const int64_t idx_col_count, const int64_t *min_max_col_idxs);
  void init_sum_meta(const int64_t idx_col_count, const int64_t *sum_col_idxs);

  void generate_row_by_seed(const int64_t seed, ObDatumRow &datum_row);
  void reset_min_max_row();
  void update_min_max_row(const ObDatumRow &row);
  void update_sum_row(const ObDatumRow &row,  ObObj *sum_res, ObObj *data);
  void validate_sum_agg_row(const ObSkipIndexAggResult &agg_row, const ObObj *sum_res, int64_t nop_col_cnt= 0, int64_t *nop_col_idxs = nullptr);
  void validate_agg_row(const ObSkipIndexAggResult &row, int64_t nop_col_cnt = 0, int64_t *nop_col_idxs = nullptr, ObSkipIndexColType *nop_col_types = nullptr);
  void set_nop_cols(ObSkipIndexAggResult &agg_row, int64_t nop_col_cnt = 0, int64_t *nop_col_idxs = nullptr, ObSkipIndexColType *nop_col_types = nullptr);
  bool is_col_in_nop_col_arr(const int64_t col_idx, const int64_t nop_col_cnt, int64_t *nop_col_idxs, int64_t &index);
  void serialize_agg_row(const ObSkipIndexAggResult &agg_data, const char *&row_buf, int64_t &row_size);
  void get_cmp_func(const ObColDesc &col_desc, ObStorageDatumCmpFunc &cmp_func);
  void str_datum_to_lob_data(const ObDatum &str_datum, ObDatum &lob_datum, ObIAllocator &alloc);

  ObArenaAllocator allocator_;
  ObRowGenerate row_generate_;
  ObArray<ObColDesc> col_descs_;
  ObArray<ObSkipIndexColMeta> full_agg_metas_;
  ObTableSchema schema_;
  const ObObjType *col_obj_types_;
  int64_t col_count_;
  int64_t rowkey_count_;
  int64_t full_column_count_;
  ObDatumRow max_row_;
  ObDatumRow min_row_;
  int64_t *null_count_arr_;
  ObAggRowWriter agg_row_writer_;
  ObMicroBlockEncodingCtx ctx_;
  ObIMicroBlockWriter *micro_writer_;
  int64_t data_version_;
};

void TestIndexBlockAggregator::init_schema(const int64_t col_count, const ObObjType *col_obj_types)
{
  col_count_ = col_count;
  rowkey_count_ = 1;
  col_obj_types_ = col_obj_types;
  ObColumnSchemaV2 col;
  schema_.reset();
  schema_.set_tenant_id(1);
  schema_.set_tablegroup_id(1);
  schema_.set_database_id(1);
  schema_.set_table_id(200001);
  schema_.set_table_name("test_index_aggregator_schema");
  schema_.set_rowkey_column_num(rowkey_count_);
  schema_.set_max_column_id(col_count_ * 2);
  schema_.set_block_size(2 * 1024);

  ObSqlString str;
  for (int64_t i = 0; i < col_count_; ++i) {
    col.reset();
    col.set_table_id(200001);
    col.set_column_id(i + OB_APP_MIN_COLUMN_ID);
    str.assign_fmt("test%ld", i);
    col.set_column_name(str.ptr());
    ObObjType type = col_obj_types_[i];
    col.set_data_type(type);
    if (ob_is_string_tc(type)) {
      col.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    } else {
      col.set_collation_type(CS_TYPE_BINARY);
    }

    if (0 == i) {
      col.set_rowkey_position(1);
    }
    ASSERT_EQ(OB_SUCCESS, schema_.add_column(col));
  }
  ASSERT_EQ(OB_SUCCESS, row_generate_.init(schema_, true/*multi_version*/));
  ASSERT_EQ(OB_SUCCESS, schema_.get_multi_version_column_descs(col_descs_));
  full_column_count_ = col_descs_.count();
}

void TestIndexBlockAggregator::init_data_encoder(
    const ObRowStoreType row_store_type,
    ObIMicroBlockWriter *&micro_writer)
{
  ctx_.micro_block_size_ = 1L << 20;  // 1MB, maximum micro block size;
  ctx_.macro_block_size_ = 2L << 20;
  ctx_.rowkey_column_cnt_ = rowkey_count_;
  ctx_.column_cnt_ = full_column_count_;
  ctx_.col_descs_ = &col_descs_;
  ctx_.major_working_cluster_version_ = cal_version(4, 3, 5, 0);
  ctx_.row_store_type_ = row_store_type;
  ctx_.compressor_type_ = common::ObCompressorType::NONE_COMPRESSOR;
  ctx_.need_calc_column_chksum_ = true;
  if (ObRowStoreType::CS_ENCODING_ROW_STORE == row_store_type) {
    micro_writer = OB_NEWx(ObMicroBlockCSEncoder, &allocator_);
    ASSERT_EQ(OB_SUCCESS, static_cast<ObMicroBlockCSEncoder *>(micro_writer)->init(ctx_));
  } else if (ObRowStoreType::ENCODING_ROW_STORE == row_store_type) {
    micro_writer = OB_NEWx(ObMicroBlockEncoder, &allocator_);
    ASSERT_EQ(OB_SUCCESS, static_cast<ObMicroBlockEncoder *>(micro_writer)->init(ctx_));
  }
  ASSERT_NE(nullptr, micro_writer);
}

void TestIndexBlockAggregator::init_min_max_meta(
    const int64_t idx_col_count, const int64_t *min_max_col_idxs)
{
  for (int64_t i = 0; i < idx_col_count; ++i) {
    ObSkipIndexColMeta meta;
    ObSkipIndexColMeta max;
    ObSkipIndexColMeta null_count;
    meta.col_idx_ = min_max_col_idxs[i];
    meta.col_type_ = SK_IDX_MIN;
    ASSERT_EQ(OB_SUCCESS, full_agg_metas_.push_back(meta));
    meta.col_type_ = SK_IDX_MAX;
    ASSERT_EQ(OB_SUCCESS, full_agg_metas_.push_back(meta));
    meta.col_type_ = SK_IDX_NULL_COUNT;
    ASSERT_EQ(OB_SUCCESS, full_agg_metas_.push_back(meta));
  }
}

void TestIndexBlockAggregator::init_sum_meta(
    const int64_t idx_col_count, const int64_t *sum_col_idxs)
{
  for (int64_t i = 0; i < idx_col_count; ++i) {
    ObSkipIndexColMeta meta;
    ObSkipIndexColMeta max;
    ObSkipIndexColMeta null_count;
    meta.col_idx_ = sum_col_idxs[i];
    meta.col_type_ = SK_IDX_SUM;
    ASSERT_EQ(OB_SUCCESS, full_agg_metas_.push_back(meta));
  }
}

void TestIndexBlockAggregator::generate_row_by_seed(const int64_t seed, ObDatumRow &datum_row)
{
  // if (0 == seed) {
  if (false) {
    for (int64_t i = 0; i < datum_row.get_column_count(); ++i) {
      datum_row.storage_datums_[i].set_null();
    }
  } else {
    ASSERT_EQ(OB_SUCCESS, row_generate_.get_next_row(seed, datum_row));
  }
}

void TestIndexBlockAggregator::reset_min_max_row()
{
  const int64_t agg_col_cnt = full_agg_metas_.count();
  min_row_.reset();
  max_row_.reset();
  if (nullptr != null_count_arr_) {
    allocator_.free(null_count_arr_);
  }
  ASSERT_EQ(OB_SUCCESS, min_row_.init(agg_col_cnt));
  ASSERT_EQ(OB_SUCCESS, max_row_.init(agg_col_cnt));
  for (int64_t i = 0; i < agg_col_cnt; ++i) {
    min_row_.storage_datums_[i].set_max();
    max_row_.storage_datums_[i].set_min();
  }
  null_count_arr_ = reinterpret_cast<int64_t *>(allocator_.alloc(sizeof(int64_t) * agg_col_cnt));
  memset(null_count_arr_, 0, sizeof(int64_t) * agg_col_cnt);
}

void TestIndexBlockAggregator::update_min_max_row(const ObDatumRow &row)
{
  for (int64_t i = 0; i < row.get_column_count(); ++i) {
    const ObStorageDatum &curr_datum = row.storage_datums_[i];
    if (!curr_datum.is_null()) {
      ObStorageDatum &min_datum = min_row_.storage_datums_[i];
      ObStorageDatum &max_datum = max_row_.storage_datums_[i];
      // if (curr_datum.len_ > ObSkipIndexColMeta::MAX_SKIP_INDEX_COL_LENGTH) {
      //   min_datum.set_null();
      //   max_datum.set_null();
      // } else {
        int min_cmp_ret = 0;
        int max_cmp_ret = 0;
        ObStorageDatumCmpFunc cmp_func;
        get_cmp_func(col_descs_.at(i), cmp_func);
        ASSERT_EQ(OB_SUCCESS, cmp_func.compare(curr_datum, min_datum, min_cmp_ret));
        ASSERT_EQ(OB_SUCCESS, cmp_func.compare(curr_datum, max_datum, max_cmp_ret));
        if (min_cmp_ret < 0) {
          min_datum.deep_copy(curr_datum, allocator_);
        }
        if (max_cmp_ret > 0) {
          max_datum.deep_copy(curr_datum, allocator_);
        }
      // }
    } else {
      null_count_arr_[i]++;
    }
  }
}

void TestIndexBlockAggregator::update_sum_row(const ObDatumRow &row,  ObObj *sum_res, ObObj *data)
{
  for (int64_t col_id = 0; col_id < row.get_column_count(); ++col_id) {
    const ObObjMeta col_type = col_descs_[col_id].col_type_;
    if (!col_type.is_numeric_type()|| col_type.get_type_class() == ObObjTypeClass::ObBitTC || row.storage_datums_[col_id].is_null()) {
    } else if (sum_res[col_id].is_null()) {
      row.storage_datums_[col_id].to_obj(sum_res[col_id], col_type);
    } else {
      row.storage_datums_[col_id].to_obj(data[col_id], col_type);
      ASSERT_EQ(OB_SUCCESS, sql::ObExprAdd::calc(sum_res[col_id], data[col_id], sum_res[col_id],
                          &allocator_, col_type.get_scale()));
    }
  }
}

void TestIndexBlockAggregator::validate_agg_row(
    const ObSkipIndexAggResult &agg_result, int64_t nop_col_cnt, int64_t *nop_col_idxs, ObSkipIndexColType *nop_col_types)
{
  const ObDatumRow &datum_row = agg_result.agg_row_;
  const ObIArray<ObSkipIndexDatumAttr> &attr_array = agg_result.attr_array_;
  for (int64_t i = 0; i < full_agg_metas_.count(); ++i) {
    ObSkipIndexColMeta idx_meta = full_agg_metas_.at(i);
    const int64_t col_idx = idx_meta.col_idx_;
    int64_t index = 0;
    const bool is_prefix = attr_array.at(i).is_min_max_prefix_;
    bool is_nop_column = is_col_in_nop_col_arr(col_idx, nop_col_cnt, nop_col_idxs, index);
    if (is_nop_column && ((nop_col_types == nullptr ) || (nop_col_types != nullptr && nop_col_types[index] == idx_meta.col_type_))) {
      ASSERT_TRUE(datum_row.storage_datums_[i].is_nop());
    } else if (datum_row.storage_datums_[i].is_nop() || datum_row.storage_datums_[i].is_null()) { // skip for not aggregate data
      if ((!min_row_.storage_datums_[col_idx].is_null()) || (max_row_.storage_datums_[col_idx].is_null())) {
        FLOG_INFO("[Salton] validate failed", K(datum_row.storage_datums_[i]),
            K(min_row_.storage_datums_[col_idx]), K(max_row_.storage_datums_[col_idx]), K(idx_meta),
            K(col_descs_.at(col_idx)));
      }
      ASSERT_TRUE(min_row_.storage_datums_[col_idx].is_null());
      ASSERT_TRUE(max_row_.storage_datums_[col_idx].is_null());
    } else {
      ObStorageDatumCmpFunc cmp_func;
      get_cmp_func(col_descs_.at(col_idx), cmp_func);
      int cmp_ret = 0;
      switch (idx_meta.col_type_) {
      case SK_IDX_MIN: {
        ASSERT_EQ(OB_SUCCESS, cmp_func.compare(
            min_row_.storage_datums_[col_idx], datum_row.storage_datums_[i], cmp_ret));
        if (is_prefix) {
          ASSERT_TRUE(min_row_.storage_datums_[col_idx].len_ > ObSkipIndexColMeta::MAX_SKIP_INDEX_COL_LENGTH);
        } else {
          if (cmp_ret != 0) {
            FLOG_INFO("[Salton] min datum validate failed", K(datum_row.storage_datums_[i]),
              K(min_row_.storage_datums_[col_idx]), K(idx_meta), K(col_descs_.at(col_idx)));
          }
          ASSERT_EQ(0, cmp_ret);
        }
        break;
      }
      case SK_IDX_MAX: {
        ASSERT_EQ(OB_SUCCESS, cmp_func.compare(
            max_row_.storage_datums_[col_idx], datum_row.storage_datums_[i], cmp_ret));
        if (is_prefix) {
          ASSERT_TRUE(max_row_.storage_datums_[col_idx].len_ > ObSkipIndexColMeta::MAX_SKIP_INDEX_COL_LENGTH);
        } else {
          ASSERT_EQ(0, cmp_ret);
        }
        break;
      }
      case SK_IDX_NULL_COUNT: {
        ASSERT_EQ(null_count_arr_[col_idx], datum_row.storage_datums_[i].get_int());
        break;
      }
      default:
        // ERROR
        ASSERT_TRUE(false);
      }
    }
  }
}

void TestIndexBlockAggregator::validate_sum_agg_row(const ObSkipIndexAggResult &agg_row, const ObObj *sum_res,
    int64_t nop_col_cnt, int64_t *nop_col_idxs)
{
  const ObDatumRow &datum_row = agg_row.agg_row_;
  for (int64_t i = 0; i < full_agg_metas_.count(); ++i) {
    ObSkipIndexColMeta idx_meta = full_agg_metas_.at(i);
    const int64_t col_idx = idx_meta.col_idx_;
    const ObObjMeta col_type = col_descs_[col_idx].col_type_;
    int64_t index = 0;
    bool is_nop_column = is_col_in_nop_col_arr(col_idx, nop_col_cnt, nop_col_idxs, index);
    if (is_nop_column || !col_type.is_numeric_type()|| col_type.get_type_class() == ObObjTypeClass::ObBitTC) {
      ASSERT_TRUE(datum_row.storage_datums_[i].is_nop());
    } else {
      const ObObjTypeClass obj_tc = col_type.get_type_class();
      switch (obj_tc) {
        case ObObjTypeClass::ObIntTC:
        case ObObjTypeClass::ObUIntTC:
        case ObObjTypeClass::ObDecimalIntTC:
        case ObObjTypeClass::ObNumberTC: {
          int cmp = 0;
          ObObj agg;
          agg.set_number(datum_row.storage_datums_[i].get_number());
          ASSERT_EQ(0, sum_res[i].compare(agg, cmp));
          break;
        }
        case ObObjTypeClass::ObFloatTC: {
          ObObj agg_obj;
          datum_row.storage_datums_[i].to_obj(agg_obj, col_type);
          int cmp = 0;
          ASSERT_EQ(0, sum_res[col_idx].compare(agg_obj, cmp));
          break;
        }
        case ObObjTypeClass::ObDoubleTC: {
          ObObj agg_obj;
          datum_row.storage_datums_[i].to_obj(agg_obj, col_type);
          int cmp = 0;
          ASSERT_EQ(0, sum_res[col_idx].compare(agg_obj, cmp));
          break;
        }
        default: {
          int ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "unexpect type", K(obj_tc));
          break;
        }
      }
    }
  }
}

void TestIndexBlockAggregator::set_nop_cols(
    ObSkipIndexAggResult &agg_row, int64_t nop_col_cnt, int64_t *nop_col_idxs, ObSkipIndexColType *nop_col_types)
{
  ObDatumRow &datum_row = agg_row.agg_row_;
  for (int64_t i = 0; i < full_agg_metas_.count(); ++i) {
    ASSERT_TRUE(i < datum_row.get_column_count());
    ObSkipIndexColMeta idx_meta = full_agg_metas_.at(i);
    const int64_t col_idx = idx_meta.col_idx_;
    int64_t index = 0;
    if (is_col_in_nop_col_arr(col_idx, nop_col_cnt, nop_col_idxs, index)) {
      if (nop_col_types != nullptr) {
        if (nop_col_types[index] == idx_meta.col_type_) {
          datum_row.storage_datums_[i].set_nop();
        }
      } else {
        datum_row.storage_datums_[i].set_nop();
      }
    }
  }
}

bool TestIndexBlockAggregator::is_col_in_nop_col_arr(
    const int64_t col_idx, const int64_t nop_col_cnt, int64_t *nop_col_idxs, int64_t &index)
{
  bool is_nop_column = false;
  for (int64_t i = 0; i < nop_col_cnt; ++i) {
    if (col_idx == nop_col_idxs[i]) {
      is_nop_column = true;
      index = i;
      break;
    }
  }
  return is_nop_column;
}

void TestIndexBlockAggregator::serialize_agg_row(
    const ObSkipIndexAggResult &agg_data, const char *&row_buf, int64_t &row_size)
{
  agg_row_writer_.reset();
  char *buf = nullptr;
  int64_t size = 0;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, agg_row_writer_.init(full_agg_metas_, agg_data, data_version_, allocator_));
  size = agg_row_writer_.get_serialize_data_size();
  buf = static_cast<char *>(allocator_.alloc(size));
  ASSERT_TRUE(nullptr != buf);
  ASSERT_EQ(OB_SUCCESS, agg_row_writer_.write_agg_data(buf, size, pos));
  row_buf = buf;
  row_size = pos;
}

void TestIndexBlockAggregator::get_cmp_func(const ObColDesc &col_desc, ObStorageDatumCmpFunc &cmp_func)
{
  sql::ObExprBasicFuncs *basic_funcs = ObDatumFuncs::get_basic_func(
      col_desc.col_type_.get_type(), col_desc.col_type_.get_collation_type());
  cmp_func.cmp_func_.cmp_func_ = lib::is_oracle_mode() ? basic_funcs->null_last_cmp_ : basic_funcs->null_first_cmp_;
}

void TestIndexBlockAggregator::str_datum_to_lob_data(const ObDatum &str_datum, ObDatum &lob_datum, ObIAllocator &alloc)
{
  const int64_t buf_len = sizeof(ObLobCommon) + str_datum.len_;
  char *buf = (char *)alloc.alloc(buf_len);
  ASSERT_TRUE(nullptr != buf);
  ObLobCommon *lob_common = new (buf) ObLobCommon();
  MEMCPY(lob_common->buffer_, str_datum.ptr_, str_datum.len_);
  lob_datum.set_lob_data(*lob_common, buf_len);
}

TEST_F(TestIndexBlockAggregator, basic_aggregate)
{
  const int64_t test_column_cnt = 24;
  const int64_t test_row_cnt = 10;
  const int64_t extra_rowkey_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  ObObjType col_obj_types[24];
  int64_t min_max_agg_col_idxs[24];
  for (int64_t i = 0; i < test_column_cnt; ++i) {
    col_obj_types[i] = static_cast<ObObjType>(i + 1);
    const int64_t agg_col_idx = i < rowkey_count_ ? i : i + extra_rowkey_cnt;
    min_max_agg_col_idxs[i] = agg_col_idx;
  }
  init_schema(test_column_cnt, col_obj_types);
  init_min_max_meta(test_column_cnt, min_max_agg_col_idxs);

  ObSkipIndexDataAggregator data_aggregator;
  ObSkipIndexIndexAggregator index_aggregator;
  ObSkipIndexAggResult data_agg_result;
  ObSkipIndexAggResult index_agg_result;
  ASSERT_EQ(OB_SUCCESS, data_agg_result.init(full_agg_metas_.count(), allocator_));
  ASSERT_EQ(OB_SUCCESS, index_agg_result.init(full_agg_metas_.count(), allocator_));

  for (int64_t test_round = 0; test_round < 7; ++test_round) {
    reset_min_max_row();
    data_agg_result.reuse();
    index_agg_result.reuse();
    ASSERT_EQ(OB_SUCCESS, data_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
    ASSERT_EQ(OB_SUCCESS, index_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));

    const ObSkipIndexAggResult *data_agg_row = nullptr;
    const ObSkipIndexAggResult *index_agg_row = nullptr;
    ObDatumRow generate_row;
    ASSERT_EQ(OB_SUCCESS, generate_row.init(full_column_count_));
    for (int64_t i = 0; i < test_row_cnt; ++i) {
      const int64_t seed = random() % test_row_cnt;
      generate_row_by_seed(seed, generate_row);
      ASSERT_EQ(OB_SUCCESS, data_aggregator.eval(generate_row));
      ASSERT_EQ(OB_SUCCESS, data_aggregator.get_aggregated_row(data_agg_row));
      ASSERT_TRUE(nullptr != data_agg_row);
      if (0 == i / 2) {
        ASSERT_EQ(OB_SUCCESS, index_aggregator.eval(*data_agg_row));
      } else {
        const char *row_buf = nullptr;
        int64_t row_size = 0;
        serialize_agg_row(*data_agg_row, row_buf, row_size);
        ASSERT_TRUE(nullptr != row_buf);
        ASSERT_EQ(OB_SUCCESS, index_aggregator.ObISkipIndexAggregator::eval(row_buf, row_size, i));
      }
      ASSERT_EQ(OB_SUCCESS, index_aggregator.get_aggregated_row(index_agg_row));
      ASSERT_TRUE(nullptr != index_agg_row);
      update_min_max_row(generate_row);
      validate_agg_row(*data_agg_row);
      validate_agg_row(*index_agg_row);
    }
    data_aggregator.reset();
    index_aggregator.reset();
  }

  // test nop agg
  const int64_t nop_col_cnt = 3;
  int64_t nop_col_idxs[nop_col_cnt] = {10, 13, 15};
  reset_min_max_row();
  data_agg_result.reuse();
  index_agg_result.reuse();
  ASSERT_EQ(OB_SUCCESS, data_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
  ASSERT_EQ(OB_SUCCESS, index_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
  const ObSkipIndexAggResult *data_agg_row = nullptr;
  const ObSkipIndexAggResult *index_agg_row = nullptr;
  ObDatumRow generate_row;
  ASSERT_EQ(OB_SUCCESS, generate_row.init(full_column_count_));
  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, data_aggregator.eval(generate_row));
    ASSERT_EQ(OB_SUCCESS, data_aggregator.get_aggregated_row(data_agg_row));
    ASSERT_TRUE(nullptr != data_agg_row);
    set_nop_cols(*const_cast<ObSkipIndexAggResult *>(data_agg_row), nop_col_cnt, nop_col_idxs);
    if (0 == i / 2) {
      ASSERT_EQ(OB_SUCCESS, index_aggregator.eval(*data_agg_row));
    } else {
      const char *row_buf = nullptr;
      int64_t row_size = 0;
      serialize_agg_row(*data_agg_row, row_buf, row_size);
      ASSERT_TRUE(nullptr != row_buf);
      ASSERT_EQ(OB_SUCCESS, index_aggregator.ObISkipIndexAggregator::eval(row_buf, row_size, i));
    }
    ASSERT_EQ(OB_SUCCESS, index_aggregator.get_aggregated_row(index_agg_row));
    ASSERT_TRUE(nullptr != index_agg_row);
    update_min_max_row(generate_row);
    validate_agg_row(*data_agg_row, nop_col_cnt, nop_col_idxs);
    validate_agg_row(*index_agg_row, nop_col_cnt, nop_col_idxs);
  }

  // test null index row
  ObSkipIndexColType nop_col_types[3] = {SK_IDX_MAX, SK_IDX_MIN, SK_IDX_NULL_COUNT};
  reset_min_max_row();
  data_agg_result.reuse();
  index_agg_result.reuse();
  data_aggregator.reuse();
  index_aggregator.reuse();
  data_agg_row = nullptr;
  index_agg_row = nullptr;
  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, data_aggregator.eval(generate_row));
    ASSERT_EQ(OB_SUCCESS, data_aggregator.get_aggregated_row(data_agg_row));
    ASSERT_TRUE(nullptr != data_agg_row);
    set_nop_cols(*const_cast<ObSkipIndexAggResult *>(data_agg_row), nop_col_cnt, nop_col_idxs, nop_col_types);
    if (0 == i / 2) {
      ASSERT_EQ(OB_SUCCESS, index_aggregator.eval(*data_agg_row));
    } else {
      const char *row_buf = nullptr;
      int64_t row_size = 0;
      serialize_agg_row(*data_agg_row, row_buf, row_size);
      ASSERT_TRUE(nullptr != row_buf);
      ASSERT_EQ(OB_SUCCESS, index_aggregator.ObISkipIndexAggregator::eval(row_buf, row_size, i));
    }
    ASSERT_EQ(OB_SUCCESS, index_aggregator.get_aggregated_row(index_agg_row));
    ASSERT_TRUE(nullptr != index_agg_row);
    update_min_max_row(generate_row);
    validate_agg_row(*data_agg_row, nop_col_cnt, nop_col_idxs, nop_col_types);
    validate_agg_row(*index_agg_row, nop_col_cnt, nop_col_idxs, nop_col_types);
  }


  // test reuse
  reset_min_max_row();
  data_agg_result.reuse();
  index_agg_result.reuse();
  data_aggregator.reuse();
  index_aggregator.reuse();
  data_agg_row = nullptr;
  index_agg_row = nullptr;
  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, data_aggregator.eval(generate_row));
    ASSERT_EQ(OB_SUCCESS, data_aggregator.get_aggregated_row(data_agg_row));
    ASSERT_TRUE(nullptr != data_agg_row);
    set_nop_cols(*const_cast<ObSkipIndexAggResult *>(data_agg_row), 0, nullptr);
    ASSERT_EQ(OB_SUCCESS, index_aggregator.eval(*data_agg_row));
    ASSERT_EQ(OB_SUCCESS, index_aggregator.get_aggregated_row(index_agg_row));
    ASSERT_TRUE(nullptr != index_agg_row);
    update_min_max_row(generate_row);
    validate_agg_row(*data_agg_row, 0, nullptr);
    validate_agg_row(*index_agg_row, 0, nullptr);
  }

}

TEST_F(TestIndexBlockAggregator, test_sum)
{
  static const int64_t test_column_cnt = 4;
  const int64_t test_row_cnt = 10;
  const int64_t extra_rowkey_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  ObObjType col_obj_types[test_column_cnt];
  col_obj_types[0] = ObIntType;
  col_obj_types[1] = ObFloatType;
  col_obj_types[2] = ObDoubleType;
  col_obj_types[3] = ObCharType;
  init_schema(test_column_cnt, col_obj_types);
  int64_t sum_col_idxs[test_column_cnt];
  for (int64_t i = 0; i < test_column_cnt; ++i) {
    const int64_t agg_col_idx = i < rowkey_count_ ? i : i + extra_rowkey_cnt;
    sum_col_idxs[i] = agg_col_idx;
  }

  ObObj data[test_column_cnt + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()];
  ObObj sum_res[test_column_cnt + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()];
  init_sum_meta(test_column_cnt, sum_col_idxs);

  ObSkipIndexDataAggregator data_aggregator;
  ObSkipIndexIndexAggregator reuse_data_aggregator;
  ObSkipIndexIndexAggregator index_aggregator;

  ObArenaAllocator allocator;
  for (int64_t test_round = 0; test_round < 7; ++test_round) {
    allocator.reuse();
    ASSERT_EQ(OB_SUCCESS, data_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
    ASSERT_EQ(OB_SUCCESS, reuse_data_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
    ASSERT_EQ(OB_SUCCESS, index_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));

    const ObSkipIndexAggResult *data_agg_row = nullptr;
    const ObSkipIndexAggResult *reuse_data_agg_row = nullptr;
    const ObSkipIndexAggResult *index_agg_row = nullptr;
    ObDatumRow generate_row;
    ASSERT_EQ(OB_SUCCESS, generate_row.init(full_column_count_));
    for (int64_t i = 0; i < test_row_cnt; ++i) {
      const int64_t seed = random() % test_row_cnt;
      generate_row_by_seed(seed, generate_row);
      update_sum_row(generate_row, sum_res, data);

      ASSERT_EQ(OB_SUCCESS, data_aggregator.eval(generate_row));
      ASSERT_EQ(OB_SUCCESS, data_aggregator.get_aggregated_row(data_agg_row));
      ASSERT_TRUE(nullptr != data_agg_row);
      const char *row_buf = nullptr;
      int64_t row_size = 0;
      serialize_agg_row(*data_agg_row, row_buf, row_size);
      ASSERT_TRUE(nullptr != row_buf);
      ASSERT_EQ(OB_SUCCESS, reuse_data_aggregator.ObISkipIndexAggregator::eval(row_buf, row_size, i));
      ASSERT_EQ(OB_SUCCESS, reuse_data_aggregator.get_aggregated_row(reuse_data_agg_row));
      ASSERT_TRUE(nullptr != reuse_data_agg_row);
      if (0 == i / 2) {
        ASSERT_EQ(OB_SUCCESS, index_aggregator.eval(*data_agg_row));
      } else {
        ASSERT_EQ(OB_SUCCESS, index_aggregator.ObISkipIndexAggregator::eval(row_buf, row_size, i));
      }
      ASSERT_EQ(OB_SUCCESS, index_aggregator.get_aggregated_row(index_agg_row));
      ASSERT_TRUE(nullptr != index_agg_row);
      validate_sum_agg_row(*data_agg_row, sum_res);
      validate_sum_agg_row(*reuse_data_agg_row, sum_res);
      validate_sum_agg_row(*index_agg_row, sum_res);
      reuse_data_aggregator.reuse();
      index_aggregator.reuse();
    }

    data_aggregator.reset();
    reuse_data_aggregator.reset();
    index_aggregator.reset();
  }

  // test nop agg
  const int64_t nop_col_cnt = 1;
  int64_t nop_col_idxs[nop_col_cnt] = {3};
  for (int64_t col_id = 0; col_id < test_column_cnt + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt(); ++col_id) {
    sum_res[col_id].set_null();
  }
  ASSERT_EQ(OB_SUCCESS, data_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
  ASSERT_EQ(OB_SUCCESS, index_aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));
  const ObSkipIndexAggResult *data_agg_row = nullptr;
  const ObSkipIndexAggResult *index_agg_row = nullptr;
  ObDatumRow generate_row;
  ASSERT_EQ(OB_SUCCESS, generate_row.init(full_column_count_));
  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    update_sum_row(generate_row, sum_res, data);
    ASSERT_EQ(OB_SUCCESS, data_aggregator.eval(generate_row));
    ASSERT_EQ(OB_SUCCESS, data_aggregator.get_aggregated_row(data_agg_row));
    ASSERT_TRUE(nullptr != data_agg_row);
    set_nop_cols(*const_cast<ObSkipIndexAggResult *>(data_agg_row), nop_col_cnt, nop_col_idxs);
    ASSERT_EQ(OB_SUCCESS, index_aggregator.eval(*data_agg_row));
    ASSERT_EQ(OB_SUCCESS, index_aggregator.get_aggregated_row(index_agg_row));
    ASSERT_TRUE(nullptr != index_agg_row);
    validate_sum_agg_row(*data_agg_row, sum_res, nop_col_cnt, nop_col_idxs);
    validate_sum_agg_row(*index_agg_row, sum_res, nop_col_cnt, nop_col_idxs);
    index_aggregator.reuse();
  }
}

TEST_F(TestIndexBlockAggregator, min_max_agg_from_encoder)
{
  const int64_t test_min_max_column_cnt = 24;
  const int64_t test_row_cnt = 10;
  const int64_t extra_rowkey_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  ObObjType col_obj_types[24];
  int64_t min_max_agg_col_idxs[24];
  for (int64_t i = 0; i < test_min_max_column_cnt; ++i) {
    col_obj_types[i] = static_cast<ObObjType>(i + 1);
    const int64_t agg_col_idx = i < rowkey_count_ ? i : i + extra_rowkey_cnt;
    min_max_agg_col_idxs[i] = agg_col_idx;
  }
  init_schema(test_min_max_column_cnt, col_obj_types);
  init_min_max_meta(test_min_max_column_cnt, min_max_agg_col_idxs);
  ObIMicroBlockWriter *cs_encoder = nullptr;
  init_data_encoder(ObRowStoreType::CS_ENCODING_ROW_STORE, cs_encoder);
  ObIMicroBlockWriter *encoder = nullptr;
  init_data_encoder(ObRowStoreType::ENCODING_ROW_STORE, encoder);

  ObSkipIndexDataAggregator aggregator;
  ObDatumRow agg_result;
  ASSERT_EQ(OB_SUCCESS, agg_result.init(full_agg_metas_.count()));
  char *block_buf = nullptr;
  int64_t block_size = 0;
  ASSERT_EQ(OB_SUCCESS, aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));

  // cs encoder min_max
  aggregator.reuse();
  reset_min_max_row();
  const ObSkipIndexAggResult *agg_row = nullptr;
  ObDatumRow generate_row;
  ASSERT_EQ(OB_SUCCESS, generate_row.init(full_column_count_));

  cs_encoder->reuse();
  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, cs_encoder->append_row(generate_row));
    update_min_max_row(generate_row);
  }
  ASSERT_EQ(OB_ERR_UNEXPECTED, aggregator.eval(*cs_encoder));
  ASSERT_EQ(OB_SUCCESS, cs_encoder->build_block(block_buf, block_size));
  ASSERT_EQ(OB_SUCCESS, aggregator.eval(*cs_encoder));
  ASSERT_EQ(OB_SUCCESS, aggregator.get_aggregated_row(agg_row));
  validate_agg_row(*agg_row);

  // encoder min_max
  aggregator.reuse();
  reset_min_max_row();

  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, encoder->append_row(generate_row));
    update_min_max_row(generate_row);
  }
  ASSERT_EQ(OB_ERR_UNEXPECTED, aggregator.eval(*encoder));
  ASSERT_EQ(OB_SUCCESS, encoder->build_block(block_buf, block_size));
  ASSERT_EQ(OB_SUCCESS, aggregator.eval(*encoder));
  ASSERT_EQ(OB_SUCCESS, aggregator.get_aggregated_row(agg_row));
  validate_agg_row(*agg_row);
}

TEST_F(TestIndexBlockAggregator, sum_agg_from_encoder)
{
  const int64_t test_column_cnt = 4;
  const int64_t test_row_cnt = 100;
  const int64_t extra_rowkey_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  ObObjType col_obj_types[test_column_cnt];
  col_obj_types[0] = ObIntType;
  col_obj_types[1] = ObFloatType;
  col_obj_types[2] = ObDoubleType;
  col_obj_types[3] = ObCharType;
  int64_t sum_col_idxs[test_column_cnt];
  for (int64_t i = 0; i < test_column_cnt; ++i) {
    col_obj_types[i] = static_cast<ObObjType>(i + 1);
    const int64_t agg_col_idx = i < rowkey_count_ ? i : i + extra_rowkey_cnt;
    sum_col_idxs[i] = agg_col_idx;
  }
  init_schema(test_column_cnt, col_obj_types);
  ObObj data[test_column_cnt + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()];
  ObObj sum_res[test_column_cnt + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()];
  init_sum_meta(test_column_cnt, sum_col_idxs);

  ObIMicroBlockWriter *cs_encoder = nullptr;
  init_data_encoder(ObRowStoreType::CS_ENCODING_ROW_STORE, cs_encoder);
  ObIMicroBlockWriter *encoder = nullptr;
  init_data_encoder(ObRowStoreType::ENCODING_ROW_STORE, encoder);

  ObSkipIndexDataAggregator aggregator;
  ObDatumRow agg_result;
  ASSERT_EQ(OB_SUCCESS, agg_result.init(full_agg_metas_.count()));
  char *block_buf = nullptr;
  int64_t block_size = 0;
  ASSERT_EQ(OB_SUCCESS, aggregator.init(full_agg_metas_, col_descs_, data_version_, allocator_));

  // cs encoder sum
  aggregator.reuse();
  const ObSkipIndexAggResult *agg_row = nullptr;
  ObDatumRow generate_row;
  ASSERT_EQ(OB_SUCCESS, generate_row.init(full_column_count_));

  cs_encoder->reuse();
  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, cs_encoder->append_row(generate_row));
    update_sum_row(generate_row, sum_res, data);
  }
  ASSERT_EQ(OB_ERR_UNEXPECTED, aggregator.eval(*cs_encoder));
  ASSERT_EQ(OB_SUCCESS, cs_encoder->build_block(block_buf, block_size));
  ASSERT_EQ(OB_SUCCESS, aggregator.eval(*cs_encoder));
  ASSERT_EQ(OB_SUCCESS, aggregator.get_aggregated_row(agg_row));
  validate_sum_agg_row(*agg_row, sum_res);

  // encoder sum
  aggregator.reuse();

  for (int64_t i = 0; i < test_row_cnt; ++i) {
    const int64_t seed = random() % test_row_cnt;
    generate_row_by_seed(seed, generate_row);
    ASSERT_EQ(OB_SUCCESS, encoder->append_row(generate_row));
    update_sum_row(generate_row, sum_res, data);
  }
  ASSERT_EQ(OB_ERR_UNEXPECTED, aggregator.eval(*encoder));
  ASSERT_EQ(OB_SUCCESS, encoder->build_block(block_buf, block_size));
  ASSERT_EQ(OB_SUCCESS, aggregator.eval(*encoder));
  ASSERT_EQ(OB_SUCCESS, aggregator.get_aggregated_row(agg_row));
  validate_sum_agg_row(*agg_row, sum_res);
}

TEST_F(TestIndexBlockAggregator, min_max_agg_calc_with_prefix)
{

  ObColDesc varchar_desc;
  varchar_desc.col_type_.set_varchar();
  varchar_desc.col_type_.set_collation_level(CS_LEVEL_IMPLICIT);
  varchar_desc.col_type_.set_collation_type(CS_TYPE_BINARY);

  ObColMinAggregator min_varchar_aggregator;
  ObStorageDatum min_varchar_agg_res;
  ObSkipIndexDatumAttr min_varchar_res_attr;
  ASSERT_EQ(OB_SUCCESS, min_varchar_aggregator.init(varchar_desc, DATA_CURRENT_VERSION, min_varchar_agg_res, min_varchar_res_attr));

  ObColMaxAggregator max_varchar_aggregator;
  ObStorageDatum max_varchar_agg_res;
  ObSkipIndexDatumAttr max_varchar_res_attr;
  ASSERT_EQ(OB_SUCCESS, max_varchar_aggregator.init(varchar_desc, DATA_CURRENT_VERSION, max_varchar_agg_res, max_varchar_res_attr));

  // TODO: add text for other types
  ObColDesc medium_text_desc;
  medium_text_desc.col_type_.set_type(ObMediumTextType);
  medium_text_desc.col_type_.set_collation_level(CS_LEVEL_IMPLICIT);
  medium_text_desc.col_type_.set_collation_type(CS_TYPE_BINARY);

  ObColMinAggregator min_text_aggregator;
  ObStorageDatum min_text_agg_res;
  ObSkipIndexDatumAttr min_text_res_attr;
  ASSERT_EQ(OB_SUCCESS, min_text_aggregator.init(medium_text_desc, DATA_CURRENT_VERSION, min_text_agg_res, min_text_res_attr));

  ObColMaxAggregator max_text_aggregator;
  ObStorageDatum max_text_agg_res;
  ObSkipIndexDatumAttr max_text_res_attr;
  ASSERT_EQ(OB_SUCCESS, max_text_aggregator.init(medium_text_desc, DATA_CURRENT_VERSION, max_text_agg_res, max_text_res_attr));

  {
    // same datum
    auto verify = [] (const ObDatum &l_datum, const ObDatum &r_datum, ObColMinAggregator &min_agg, ObColMaxAggregator &max_agg)
    {
      int cmp_ret = 0;
      // min
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, false, false, cmp_ret));
      ASSERT_EQ(cmp_ret, 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, true, true, cmp_ret));
      ASSERT_EQ(cmp_ret, 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, true, false, cmp_ret));
      ASSERT_TRUE(cmp_ret > 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, false, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      // max
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, false, false, cmp_ret));
      ASSERT_EQ(cmp_ret, 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, true, true, cmp_ret));
      ASSERT_EQ(cmp_ret, 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, true, false, cmp_ret));
      ASSERT_TRUE(cmp_ret > 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, false, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
    };

    const char *str1 = "abc";
    ObDatum l_datum(str1, strlen(str1), false);
    ObDatum r_datum(str1, strlen(str1), false);
    verify(l_datum, r_datum, min_varchar_aggregator, max_varchar_aggregator);

    // text
    ObDatum lt_datum;
    ObDatum rt_datum;
    str_datum_to_lob_data(l_datum, lt_datum, allocator_);
    str_datum_to_lob_data(r_datum, rt_datum, allocator_);
    verify(lt_datum, rt_datum, min_text_aggregator, max_text_aggregator);
  }

  {
    // different length
    auto verify = [] (const ObDatum &l_datum, const ObDatum &r_datum, ObColMinAggregator &min_agg, ObColMaxAggregator &max_agg)
    {
      int cmp_ret = 0;
      // min
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, false, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, true, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, true, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, false, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      // max
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, false, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, true, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, true, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, false, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
    };

    const char *str_s = "abc";
    const char *str_l = "dddd";
    ObDatum l_datum(str_s, strlen(str_s), false);
    ObDatum r_datum(str_l, strlen(str_l), false);
    verify(l_datum, r_datum, min_varchar_aggregator, max_varchar_aggregator);

    //text
    ObDatum lt_datum;
    ObDatum rt_datum;
    str_datum_to_lob_data(l_datum, lt_datum, allocator_);
    str_datum_to_lob_data(r_datum, rt_datum, allocator_);
    verify(lt_datum, rt_datum, min_text_aggregator, max_text_aggregator);
  }

  {
    // different length with same prefix
    auto verify = [] (const ObDatum &l_datum, const ObDatum &r_datum, ObColMinAggregator &min_agg, ObColMaxAggregator &max_agg)
    {
      int cmp_ret = 0;
      // min
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, false, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, true, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, true, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, min_agg.cmp_with_prefix(l_datum, r_datum, false, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      // max
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, false, false, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, true, true, cmp_ret));
      ASSERT_TRUE(cmp_ret > 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, true, false, cmp_ret));
      ASSERT_TRUE(cmp_ret > 0);
      ASSERT_EQ(OB_SUCCESS, max_agg.cmp_with_prefix(l_datum, r_datum, false, true, cmp_ret));
      ASSERT_TRUE(cmp_ret < 0);
    };

    const char *str_s = "abc";
    const char *str_l = "abcd";
    ObDatum l_datum(str_s, strlen(str_s), false);
    ObDatum r_datum(str_l, strlen(str_l), false);
    verify(l_datum, r_datum, min_varchar_aggregator, max_varchar_aggregator);

    //text
    ObDatum lt_datum;
    ObDatum rt_datum;
    str_datum_to_lob_data(l_datum, lt_datum, allocator_);
    str_datum_to_lob_data(r_datum, rt_datum, allocator_);
    verify(lt_datum, rt_datum, min_text_aggregator, max_text_aggregator);
  }
}

}
}

int main(int argc, char **argv)
{
  system("rm -f test_index_block_aggregator.log*");
  OB_LOGGER.set_file_name("test_index_block_aggregator.log", true, false);
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}