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

#ifndef OB_TABLE_DML_PARAM_H_
#define OB_TABLE_DML_PARAM_H_

#include "ob_schema_struct.h"
#include "ob_table_schema.h"
#include "share/schema/ob_table_param.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObColumnParam;
class ObTableSchemaParam
{
  OB_UNIS_VERSION_V(1);
public:
  typedef common::ObFixedArray<common::ObRowkeyInfo *, common::ObIAllocator> RowKeys;
  typedef common::ObFixedArray<ObColumnParam *, common::ObIAllocator> Columns;
  typedef common::ObFixedArray<int32_t, common::ObIAllocator> Projector;
  typedef common::ObFixedArray<int32_t, common::ObIAllocator> ColumnsIndex;
  typedef common::ObFixedArray<ObColDesc, common::ObIAllocator> ObColDescArray;

  explicit ObTableSchemaParam(common::ObIAllocator &allocator);
  virtual ~ObTableSchemaParam();
  void reset();
  int convert(const ObTableSchema *schema);
  OB_INLINE bool is_valid() const { return common::OB_INVALID_ID != table_id_; }
  OB_INLINE bool is_global_index_table() const
  {
    return ObSimpleTableSchemaV2::is_global_index_table(index_type_);
  }
  OB_INLINE uint64_t get_table_id() const { return table_id_; }
  OB_INLINE int64_t get_schema_version() const { return schema_version_; }
  OB_INLINE ObTableType get_table_type() const { return table_type_; }
  OB_INLINE ObIndexType get_index_type() const { return index_type_; }
  OB_INLINE ObIndexStatus get_index_status() const { return index_status_; }
  OB_INLINE int64_t get_rowkey_column_num() const { return read_info_.get_schema_rowkey_count(); }
  OB_INLINE int64_t get_shadow_rowkey_column_num() const { return shadow_rowkey_column_num_; }
  OB_INLINE int64_t get_data_table_rowkey_column_num() const { return data_table_rowkey_column_num_; }
  OB_INLINE void set_data_table_rowkey_column_num(int64_t cnt) { data_table_rowkey_column_num_ = cnt; }
  OB_INLINE int64_t get_doc_id_col_id() const { return doc_id_col_id_; }
  OB_INLINE int64_t get_fulltext_col_id() const { return fulltext_col_id_; }
  OB_INLINE const common::ObString &get_fts_parser_name() const { return fts_parser_name_; }
  OB_INLINE const common::ObString &get_fts_parser_property() const { return fts_parser_properties_; }
  OB_INLINE uint64_t get_spatial_geo_col_id() const { return spatial_geo_col_id_; }
  OB_INLINE uint64_t get_spatial_cellid_col_id() const { return spatial_cellid_col_id_; }
  OB_INLINE uint64_t get_spatial_mbr_col_id() const { return spatial_mbr_col_id_; }
  OB_INLINE int64_t get_multivalue_col_id() const { return multivalue_col_id_; }
  OB_INLINE int64_t get_multivalue_array_col_id() const { return multivalue_arr_col_id_; }
  OB_INLINE int64_t get_vec_id_col_id() const { return vec_id_col_id_; }
  OB_INLINE int64_t get_vec_vector_col_id() const { return vec_vector_col_id_; }
  OB_INLINE ObString get_vec_index_param() const { return vec_index_param_; }
  OB_INLINE int64_t get_vec_dim() const { return vec_dim_; }
  OB_INLINE int64_t get_lob_inrow_threshold() const { return lob_inrow_threshold_; }
  OB_INLINE int64_t get_column_count() const { return columns_.count(); }
  OB_INLINE const Columns &get_columns() const { return columns_; }
  OB_INLINE const ColumnMap &get_col_map() const { return col_map_; }
  OB_INLINE bool is_index_table() const { return share::schema::is_index_table(table_type_); }
  OB_INLINE bool is_lob_meta_table() const { return share::schema::is_aux_lob_meta_table(table_type_); }
  OB_INLINE bool is_materialized_view() const
  { return ObTableSchema::is_materialized_view(table_type_); }
  OB_INLINE bool is_mlog_table() const
  { return ObTableSchema::is_mlog_table(table_type_); }
  OB_INLINE bool is_storage_index_table() const
  { return is_index_table(); }
  OB_INLINE bool can_read_index() const { return ObTableSchema::can_read_index(index_status_); }
  OB_INLINE bool is_unique_index() const { return ObTableSchema::is_unique_index(index_type_); }
  OB_INLINE bool is_domain_index() const { return ObTableSchema::is_domain_index(index_type_); }
  OB_INLINE bool is_spatial_index() const { return ObTableSchema::is_spatial_index(index_type_); }
  OB_INLINE bool is_fts_index() const { return share::schema::is_fts_index(index_type_); }
  OB_INLINE bool is_doc_rowkey() const { return share::schema::is_doc_rowkey_aux(index_type_); }
  OB_INLINE bool is_fts_index_aux() const { return share::schema::is_fts_index_aux(index_type_); }
  OB_INLINE bool is_fts_doc_word_aux() const { return share::schema::is_fts_doc_word_aux(index_type_); }
  OB_INLINE bool is_multivalue_index() const { return share::schema::is_multivalue_index(index_type_); }
  OB_INLINE bool is_multivalue_index_aux() const { return share::schema::is_multivalue_index_aux(index_type_); }
  OB_INLINE bool is_vector_delta_buffer() const { return share::schema::is_vec_delta_buffer_type(index_type_); }
  OB_INLINE bool is_vector_index_id() const { return share::schema::is_vec_index_id_type(index_type_); }
  OB_INLINE bool is_vector_index_snapshot() const { return share::schema::is_vec_index_snapshot_data_type(index_type_); }
  OB_INLINE bool is_index_local_storage() const { return share::schema::is_index_local_storage(index_type_); }
  OB_INLINE bool is_vector_index() const { return share::schema::is_vec_index(index_type_); }
  OB_INLINE bool is_sparse_vector_index() const { return share::schema::is_vec_spiv_index(index_type_); }
  OB_INLINE bool is_ivf_vector_index() const { return share::schema::is_vec_ivf_index(index_type_); }
  OB_INLINE bool is_no_need_update_vector_index() const
  {
    return share::schema::is_vec_index_id_type(index_type_) ||
           share::schema::is_vec_index_snapshot_data_type(index_type_) ||
           share::schema::is_vec_ivfflat_centroid_index(index_type_) ||
           share::schema::is_vec_ivfsq8_centroid_index(index_type_) ||
           share::schema::is_vec_ivfsq8_meta_index(index_type_) ||
           share::schema::is_vec_ivfpq_centroid_index(index_type_) ||
           share::schema::is_vec_ivfpq_pq_centroid_index(index_type_);
  }
  int is_rowkey_column(const uint64_t column_id, bool &is_rowkey) const;
  int is_column_nullable_for_write(const uint64_t column_id, bool &is_nullable_for_write) const;
  OB_INLINE ObMvMode get_mv_mode() const { return mv_mode_; }
  OB_INLINE bool is_delete_insert() const { return is_delete_insert_; }
  OB_INLINE const common::ObString &get_index_name() const { return index_name_; }

  const ObColumnParam * get_column(const uint64_t column_id) const;
  const ObColumnParam * get_column_by_idx(const int64_t idx) const;
  ObColumnParam * get_column_by_idx(const int64_t idx);
  const ObColumnParam * get_rowkey_column_by_idx(const int64_t idx) const;
  int get_rowkey_column_ids(common::ObIArray<ObColDesc> &column_ids) const;
  int get_rowkey_column_ids(common::ObIArray<uint64_t> &column_ids) const;
  int get_index_name(common::ObString &index_name) const;
  const common::ObString &get_pk_name() const;
  bool is_depend_column(uint64_t column_id) const;
  const storage::ObTableReadInfo &get_read_info() const
  { return read_info_; }
  inline const common::ObIArray<storage::ObTableReadInfo *> *get_cg_read_infos() const
  { return cg_read_infos_.empty() ? nullptr : &cg_read_infos_; }
  int has_udf_column(bool &has_udf) const;

  DECLARE_TO_STRING;
private:
  ObTableSchemaParam();
  DISALLOW_COPY_AND_ASSIGN(ObTableSchemaParam);

private:
  common::ObIAllocator &allocator_;
  uint64_t table_id_;
  int64_t schema_version_;
  ObTableType table_type_;
  ObIndexType index_type_;
  ObIndexStatus index_status_;
  int64_t shadow_rowkey_column_num_;
  uint64_t doc_id_col_id_;
  uint64_t fulltext_col_id_;
  uint64_t spatial_geo_col_id_; // geometry column id in data table_schema.
  uint64_t spatial_cellid_col_id_; // cellid column id in index table_schema.
  uint64_t spatial_mbr_col_id_; // mbr column id in index table_schema.
  common::ObString index_name_;
  common::ObString fts_parser_name_;
  common::ObString fts_parser_properties_;
  //generated storage param from columns_ids_ in ObTableModify, for performance improvement
  Columns columns_;
  ColumnMap col_map_;
  common::ObString pk_name_; // use for printing error msg in oracle mode
  // version of the mixture read info
  int16_t read_param_version_;
  storage::ObTableReadInfo read_info_;
  storage::ObFixedMetaObjArray<storage::ObTableReadInfo *> cg_read_infos_;
  int64_t lob_inrow_threshold_;
  uint64_t multivalue_col_id_;
  uint64_t multivalue_arr_col_id_;
  int64_t data_table_rowkey_column_num_;
  uint64_t vec_id_col_id_;
  ObString vec_index_param_;
  int64_t vec_dim_;
  uint64_t vec_vector_col_id_;
  ObMvMode mv_mode_;
  bool is_delete_insert_;
};

class ObTableDMLParam
{
  OB_UNIS_VERSION_V(1);
public:
  typedef common::ObFixedArray<ObTableSchemaParam *, common::ObIAllocator> TableSchemas;
  typedef common::ObFixedArray<ObColDesc, common::ObIAllocator> ObColDescArray;

  explicit ObTableDMLParam(common::ObIAllocator &allocator);
  virtual ~ObTableDMLParam();
  void reset();
  int convert(const ObTableSchema *table_schema,
              const int64_t tenant_schema_version,
              const common::ObIArray<uint64_t> &column_ids);
  // storage param is generated from other param, they won't be serialized.
  // it is called in convert or after deserialization
  int prepare_storage_param(const common::ObIArray<uint64_t> &column_ids);
  int set_data_table_rowkey_tags(share::schema::ObSchemaGetterGuard *guard,
                                 const ObTableSchema *index_schema,
                                 const uint64_t tenant_id);
  OB_INLINE bool is_valid() const { return data_table_.is_valid(); }
  OB_INLINE const ObTableSchemaParam & get_data_table() const { return data_table_; }
  OB_INLINE ObTableSchemaParam& get_data_table_ref() { return data_table_; }
  OB_INLINE const ObColDescArray & get_col_descs() const { return col_descs_; }
  OB_INLINE const ColumnMap &get_col_map() const { return col_map_; }
  DECLARE_TO_STRING;

private:
  ObTableDMLParam();
  DISALLOW_COPY_AND_ASSIGN(ObTableDMLParam);

private:
  common::ObIAllocator &allocator_;
  int64_t tenant_schema_version_;
  ObTableSchemaParam data_table_;

  //generated storage param from columns_ids_ in ObTableModify, for performance improvement
  ObColDescArray col_descs_;
  ColumnMap col_map_;
};
}//namespace oceanbase
}//namespace share
}//namespace schema
#endif /* OB_TABLE_DML_PARAM_H_ */
