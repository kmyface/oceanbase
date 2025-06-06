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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_sql_context.h"

#include "share/catalog/ob_cached_catalog_meta_getter.h"
#include "share/external_table/ob_external_object_ctx.h"
#include "sql/optimizer/ob_log_plan.h"
#include "sql/ob_sql_mock_schema_utils.h"

using namespace ::oceanbase::common;
namespace oceanbase
{
using namespace share::schema;
namespace sql
{

bool LocationConstraint::operator==(const LocationConstraint &other) const {
  return key_ == other.key_ && phy_loc_type_ == other.phy_loc_type_ && constraint_flags_ == other.constraint_flags_ ;
}

bool LocationConstraint::operator!=(const LocationConstraint &other) const {
  return !(*this == other);
}

int LocationConstraint::calc_constraints_inclusion(const ObLocationConstraint *left,
                                                   const ObLocationConstraint *right,
                                                   InclusionType &inclusion_result)
{
  int ret = OB_SUCCESS;
  inclusion_result = NotSubset;
  if (OB_ISNULL(left) || OB_ISNULL(right)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(left), K(right));
  } else {
    const ObLocationConstraint *set1 = NULL, *set2 = NULL;
    bool is_subset = true;
    // insure set1.count() >= set2.count()
    if (left->count() >= right->count()) {
      inclusion_result = LeftIsSuperior;
      set1 = left;
      set2 = right;
    } else {
      inclusion_result = RightIsSuperior;
      set1 = right;
      set2 = left;
    }

    for (int64_t i = 0; is_subset && i < set2->count(); i++) {
      bool detected = false;
      for (int64_t j = 0; !detected && j < set1->count(); j++) {
        if (set2->at(i) == set1->at(j)) {
          detected = true;
        }
      }
      // if the element is not in set1, set1 can not contain all the elements in set2
      if (!detected) {
        is_subset = false;
      }
    }
    if (!is_subset) {
      inclusion_result = NotSubset;
    }
  }

  return ret;
}

int ObLocationConstraintContext::calc_constraints_inclusion(const ObPwjConstraint *left,
                                                            const ObPwjConstraint *right,
                                                            InclusionType &inclusion_result)
{
  int ret = OB_SUCCESS;
  inclusion_result = NotSubset;
  if (OB_ISNULL(left) || OB_ISNULL(right)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(left), K(right));
  } else {
    const ObPwjConstraint *set1 = NULL, *set2 = NULL;
    bool is_subset = true;
    // insure set1.count() >= set2.count()
    if (left->count() >= right->count()) {
      inclusion_result = LeftIsSuperior;
      set1 = left;
      set2 = right;
    } else {
      inclusion_result = RightIsSuperior;
      set1 = right;
      set2 = left;
    }

    for (int64_t i = 0; is_subset && i < set2->count(); i++) {
      bool detected = false;
      for (int64_t j = 0; !detected && j < set1->count(); j++) {
        if (set2->at(i) == set1->at(j)) {
          detected = true;
        }
      }
      // if the element is not in set1, set1 can not contain all the elements in set2
      if (!detected) {
        is_subset = false;
      }
    }
    if (!is_subset) {
      inclusion_result = NotSubset;
    }
  }

  return ret;
}

int ObQueryRetryInfo::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("init twice", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

void ObQueryRetryInfo::reset()
{
  inited_ = false;
  is_rpc_timeout_ = false;
  last_query_retry_err_ = OB_SUCCESS;
  retry_cnt_ = 0;
  query_switch_leader_retry_timeout_ts_ = 0;
  query_retry_ash_info_.reset();
}

void ObQueryRetryInfo::clear()
{
  // 这里不能将inited_设为false
  is_rpc_timeout_ = false;
  //last_query_retry_err_ = OB_SUCCESS;
}

void ObQueryRetryInfo::set_is_rpc_timeout(bool is_rpc_timeout)
{
  is_rpc_timeout_ = is_rpc_timeout;
}

bool ObQueryRetryInfo::is_rpc_timeout() const
{
  return is_rpc_timeout_;
}

ObSqlCtx::ObSqlCtx()
  : session_info_(NULL),
    schema_guard_(NULL),
    secondary_namespace_(NULL),
    plan_cache_hit_(false),
    self_add_plan_(false),
    disable_privilege_check_(PRIV_CHECK_FLAG_NORMAL),
    force_print_trace_(false),
    is_show_trace_stmt_(false),
    retry_times_(OB_INVALID_COUNT),
    exec_type_(InvalidType),
    is_prepare_protocol_(false),
    is_pre_execute_(false),
    is_prepare_stage_(false),
    is_dynamic_sql_(false),
    is_dbms_sql_(false),
    is_cursor_(false),
    is_remote_sql_(false),
    statement_id_(common::OB_INVALID_ID),
    stmt_type_(stmt::T_NONE),
    is_restore_(false),
    need_late_compile_(false),
    all_plan_const_param_constraints_(nullptr),
    all_possible_const_param_constraints_(nullptr),
    all_equal_param_constraints_(nullptr),
    all_pre_calc_constraints_(nullptr),
    all_expr_constraints_(nullptr),
    all_priv_constraints_(nullptr),
    need_match_all_params_(false),
    all_local_session_vars_(nullptr),
    is_ddl_from_primary_(false),
    cur_stmt_(NULL),
    cur_plan_(nullptr),
    can_reroute_sql_(false),
    is_sensitive_(false),
    is_protocol_weak_read_(false),
    flashback_query_expr_(nullptr),
    is_execute_call_stmt_(false),
    enable_sql_resource_manage_(false),
    resource_map_rule_(),
    is_text_ps_mode_(false),
    first_plan_hash_(0),
    is_bulk_(false),
    ins_opt_ctx_(),
    flags_(0),
    reroute_info_(nullptr)
{
  sql_id_[0] = '\0';
  sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
  format_sql_id_[0] = '\0';
  format_sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
}

void ObSqlCtx::reset()
{
  multi_stmt_item_.reset();
  session_info_ = NULL;
  schema_guard_ = NULL;
  plan_cache_hit_ = false;
  self_add_plan_ = false;
  disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  force_print_trace_ = false;
  is_show_trace_stmt_ = false;
  retry_times_ = OB_INVALID_COUNT;
  sql_id_[0] = '\0';
  sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
  format_sql_id_[0] = '\0';
  format_sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
  exec_type_ = InvalidType;
  is_prepare_protocol_ = false;
  is_pre_execute_ = false;
  is_prepare_stage_ = false;
  is_dynamic_sql_ = false;
  is_remote_sql_ = false;
  is_restore_ = false;
  need_late_compile_ = false;
  all_plan_const_param_constraints_ = nullptr;
  all_possible_const_param_constraints_ = nullptr;
  all_equal_param_constraints_ = nullptr;
  all_pre_calc_constraints_ = nullptr;
  all_expr_constraints_ = nullptr;
  all_priv_constraints_ = nullptr;
  need_match_all_params_ = false;
  all_local_session_vars_ = nullptr;
  is_ddl_from_primary_ = false;
  can_reroute_sql_ = false;
  is_sensitive_ = false;
  enable_sql_resource_manage_ = false;
  resource_map_rule_.reset();
  is_protocol_weak_read_ = false;
  first_plan_hash_ = 0;
  first_outline_data_.reset();
  first_equal_param_cons_cnt_ = 0;
  first_const_param_cons_cnt_ = 0;
  first_expr_cons_cnt_ = 0;
  if (nullptr != reroute_info_) {
    reroute_info_->reset();
    op_reclaim_free(reroute_info_);
    reroute_info_ = nullptr;
  }
  clear();
  flashback_query_expr_ = nullptr;
  stmt_type_ = stmt::T_NONE;
  cur_plan_ = nullptr;
  is_execute_call_stmt_ = false;
  is_text_ps_mode_ = false;
  enable_strict_defensive_check_ = false;
  enable_user_defined_rewrite_ = false;
  is_bulk_ = false;
  ins_opt_ctx_.reset();
}

//release dynamic allocated memory
void ObSqlCtx::clear()
{
  partition_infos_.reset();
  related_user_var_names_.reset();
  base_constraints_.reset();
  strict_constraints_.reset();
  non_strict_constraints_.reset();
  dup_table_replica_cons_.reset();
  multi_stmt_rowkey_pos_.reset();
  spm_ctx_.bl_key_.reset();
  cur_stmt_ = nullptr;
  is_text_ps_mode_ = false;
  ins_opt_ctx_.clear();
  cur_plan_ = nullptr;
}

OB_SERIALIZE_MEMBER(ObSqlCtx, stmt_type_);

void ObSqlSchemaGuard::reset()
{
  mocked_database_schemas_.reset();
  table_schemas_.reset();
  schema_guard_ = NULL;
  allocator_.reset();
  next_link_table_id_ = 1;
  dblink_scn_.reuse();
  mocked_schema_id_counter_ = OB_MIN_EXTERNAL_OBJECT_ID;
}

TableItem *ObSqlSchemaGuard::get_table_item_by_ref_id(const ObDMLStmt *stmt, uint64_t ref_table_id)
{
  TableItem *table_item = NULL;
  if (NULL != stmt) {
   const common::ObIArray<sql::TableItem*> &table_items = stmt->get_table_items();
    int64_t num = table_items.count();
    for (int64_t i = 0; i < num; ++i) {
      if (table_items.at(i) != NULL && table_items.at(i)->ref_id_ == ref_table_id) {
        table_item = table_items.at(i);
        break;
      }
    }
  }
  return table_item;
}

bool ObSqlSchemaGuard::is_link_table(const ObDMLStmt *stmt, uint64_t table_id)
{
  bool is_link = false;
  TableItem *table_item = NULL;
  if (NULL != stmt) {
    table_item = stmt->get_table_item_by_id(table_id);
    is_link = (NULL == table_item) ? false : table_item->is_link_table();
  }
  return is_link;
}

int ObSqlSchemaGuard::get_dblink_schema(const uint64_t tenant_id,
                                        const uint64_t dblink_id,
                                        const share::schema::ObDbLinkSchema *&dblink_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null schema guard", K(ret));
  } else if (OB_FAIL(schema_guard_->get_dblink_schema(tenant_id,
                                                      dblink_id,
                                                      dblink_schema))) {
    LOG_WARN("failed to get dblink schema", K(ret));
  }
  return ret;
}

int ObSqlSchemaGuard::set_link_table_schema(uint64_t dblink_id,
                                            const common::ObString &database_name,
                                            share::schema::ObTableSchema *table_schema)
{
  int ret = OB_SUCCESS;
  table_schema->set_dblink_id(dblink_id);
  table_schema->set_table_type(share::schema::ObTableType::USER_TABLE);
  OX(table_schema->set_link_database_name(database_name);)
  OX (table_schema->set_table_id(next_link_table_id_++));
  OX (table_schema->set_link_table_id(table_schema->get_table_id()));
  OV (table_schema->get_table_id() != OB_INVALID_ID,
      OB_ERR_UNEXPECTED, dblink_id, next_link_table_id_);
  if (OB_FAIL(table_schemas_.push_back(table_schema))) {
    LOG_WARN("failed to push back table schema", K(ret));
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t dblink_id,
                                         const ObString &database_name,
                                         const ObString &table_name,
                                         const ObTableSchema *&table_schema,
                                         sql::ObSQLSessionInfo *session_info,
                                         const ObString &dblink_name,
                                         bool is_reverse_link)
{
  int ret = OB_SUCCESS;
  int64_t schema_count = table_schemas_.count();
  table_schema = NULL;
  const uint64_t tenant_id = MTL_ID();
  for (int64_t i = 0; OB_SUCC(ret) && OB_ISNULL(table_schema) && i < schema_count; i++) {
    // database_name和table_name直接调用compare接口，使用memcmp语义比较，避免多字符集导致各种问题。
    const ObTableSchema *tmp_schema = table_schemas_.at(i);
    OV (OB_NOT_NULL(tmp_schema));
    if (OB_SUCC(ret) && dblink_id == tmp_schema->get_dblink_id() &&
        0 == database_name.compare(tmp_schema->get_link_database_name()) &&
        0 == table_name.compare(tmp_schema->get_table_name_str())) {
      table_schema = tmp_schema;
    }
  }
  if (OB_SUCC(ret) && OB_ISNULL(table_schema)) {
    ObTableSchema *tmp_schema = NULL;
    OV (OB_NOT_NULL(schema_guard_), OB_NOT_INIT);
    uint64_t current_scn = OB_INVALID_ID;
    uint64_t *scn = NULL;
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(session_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("session info is null", K(ret));
      } else {
         bool use_scn = (session_info->is_in_transaction() &&
          transaction::ObTxIsolationLevel::RC == session_info->get_tx_desc()->get_isolation_level())
          || !session_info->is_in_transaction();
        if (use_scn && OB_FAIL(get_link_current_scn(dblink_id, tenant_id, session_info, current_scn))) {
          if (OB_HASH_NOT_EXIST == ret) {
            scn = &current_scn;
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("get link current scn failed", K(ret));
          }
        }
      }
    }
    OZ (schema_guard_->get_link_table_schema(tenant_id,
                                             dblink_id,
                                             database_name, table_name,
                                             allocator_, tmp_schema,
                                             session_info,
                                             dblink_name,
                                             is_reverse_link,
                                             scn));
    if (OB_SUCC(ret) && (NULL != scn)) {
      if (OB_FAIL(dblink_scn_.set_refactored(dblink_id, *scn))) {
        LOG_WARN("set refactored failed", K(ret));
      } else {
        LOG_TRACE("set dblink current scn", K(dblink_id), K(*scn));
      }
    }
    OV (OB_NOT_NULL(tmp_schema));
    OX (tmp_schema->set_table_id(next_link_table_id_++));
    OX (tmp_schema->set_link_table_id(tmp_schema->get_table_id()));
    OV (tmp_schema->get_table_id() != OB_INVALID_ID,
        OB_ERR_UNEXPECTED, dblink_id, next_link_table_id_);
    OZ (table_schemas_.push_back(tmp_schema));
    OX (table_schema = tmp_schema);
  }
  return ret;
}

int ObSqlSchemaGuard::add_mocked_table_schema(const ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  ObTableSchema *temp_schema = NULL;
  OZ (ObSchemaUtils::alloc_schema(allocator_, table_schema, temp_schema));
  OZ (table_schemas_.push_back(temp_schema));
  return ret;
}

int ObSqlSchemaGuard::add_mocked_database_schema(const share::schema::ObDatabaseSchema &database_schema)
{
  int ret = OB_SUCCESS;
  ObDatabaseSchema *tmp_schema = NULL;
  OZ(ObSchemaUtils::alloc_schema(allocator_, database_schema, tmp_schema));
  OZ(mocked_database_schemas_.push_back(tmp_schema));
  return ret;
}

int ObSqlSchemaGuard::get_mocked_table_schema(uint64_t ref_table_id, const ObTableSchema *&table_schema) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  for (int i = 0; OB_SUCC(ret) && i < table_schemas_.count(); i++) {
    const ObTableSchema *cur_table_schema = table_schemas_.at(i);
    if (OB_NOT_NULL(cur_table_schema) && cur_table_schema->get_table_id() == ref_table_id) {
      table_schema = cur_table_schema;
      break;
    }
  }
  OV (OB_NOT_NULL(table_schema));
  return ret;
}

int ObSqlSchemaGuard::recover_schema_from_external_object(const share::ObExternalObject &external_object)
{
  int ret = OB_SUCCESS;
  switch (external_object.type) {
    case share::ObExternalObjectType::TABLE_SCHEMA: {
      const uint64_t tenant_id = external_object.tenant_id;
      const uint64_t catalog_id = external_object.catalog_id;
      const uint64_t database_id = external_object.database_id;
      const common::ObString table_name = external_object.table_name;
      const uint64_t table_id = external_object.table_id;
      const ObTableSchema *table_schema = NULL;
      if (OB_FAIL(get_catalog_table_schema(tenant_id, catalog_id, database_id, table_name, table_schema))) {
        LOG_WARN("get catalog table schema failed", K(ret));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema is null", K(ret));
      } else {
        // reset table_id, because of sql_schema_guard.get_catalog_table_schema will reassign table_id
        ObTableSchema *non_const_table_schema = const_cast<ObTableSchema *>(table_schema);
        non_const_table_schema->set_table_id(table_id);
      }
      break;
    }
    case share::ObExternalObjectType::DATABASE_SCHEMA: {
      const uint64_t tenant_id = external_object.tenant_id;
      const uint64_t catalog_id = external_object.catalog_id;
      const uint64_t database_id = external_object.database_id;
      const common::ObString database_name = external_object.database_name;
      const ObDatabaseSchema *db_schema = NULL;
      if (OB_FAIL(get_catalog_database_schema(tenant_id, catalog_id, database_name, db_schema))) {
        LOG_WARN("get catalog database schema failed", K(ret));
      } else if (OB_ISNULL(db_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("database schema is null", K(ret));
      } else {
        ObDatabaseSchema *non_const_db_schema = const_cast<ObDatabaseSchema *>(db_schema);
        // reset database_id, because of sql_schema_guard.get_catalog_database_schema will reassign database_id
        non_const_db_schema->set_database_id(database_id);
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected", K(ret));
    }
  }
  return ret;
}

int ObSqlSchemaGuard::recover_schema_from_external_objects(const ObIArray<share::ObExternalObject> &external_objects)
{
  int ret = OB_SUCCESS;
  // recover mocked database schema first, because mocked table schema rely on database schema
  for (int64_t i = 0; OB_SUCC(ret) && i < external_objects.count(); i++) {
    const share::ObExternalObject &external_object = external_objects.at(i);
    if (external_object.type == share::ObExternalObjectType::DATABASE_SCHEMA) {
      OZ(recover_schema_from_external_object(external_object));
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < external_objects.count(); i++) {
    const share::ObExternalObject &external_object = external_objects.at(i);
    if (external_object.type == share::ObExternalObjectType::TABLE_SCHEMA) {
      OZ(recover_schema_from_external_object(external_object));
    }
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t table_id,
                                      uint64_t ref_table_id,
                                      const ObDMLStmt *stmt,
                                      const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;;
    LOG_WARN("get unexpected null", K(ret), K(stmt));
  } else {
    const TableItem *item = stmt->get_table_item_by_id(table_id);
    if (NULL != item && item->is_link_table()) {
      if (OB_FAIL(get_link_table_schema(ref_table_id, table_schema))) {
        LOG_WARN("failed to get link table schema", K(table_id), K(ret));
      }
    } else if (is_external_object_id(table_id)) {
      if (OB_FAIL(get_mocked_table_schema(ref_table_id, table_schema))) {
        LOG_WARN("failed to get mocked table schema", K(ref_table_id), K(ret));
      }
    } else if (OB_FAIL(get_table_schema(ref_table_id, table_schema))) {
      LOG_WARN("failed to get table schema", K(table_id), K(ret));
    }
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t table_id,
                                      const TableItem *table_item,
                                      const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_item) ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get unexpected null", K(ret), K(table_item));
  } else if (table_item->is_link_table()) {
    if (OB_FAIL(get_link_table_schema(table_id, table_schema))) {
      LOG_WARN("failed to get link table schema", K(table_id), K(ret));
    }
  } else if (is_external_object_id(table_id)) {
    if (OB_FAIL(get_mocked_table_schema(table_id, table_schema))) {
      LOG_WARN("failed to get mocked table schema", K(table_id), K(ret));
    }
  } else if (OB_FAIL(get_table_schema(table_id, table_schema))) {
    LOG_WARN("failed to get table schema", K(table_id), K(ret));
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t table_id,
                                         const ObTableSchema *&table_schema,
                                         bool is_link /* = false*/) const
{
  int ret = OB_SUCCESS;
  if (is_link) {
    OZ (get_link_table_schema(table_id, table_schema), table_id, is_link);
  } else if (is_external_object_id(table_id)) {
    if (OB_FAIL(get_mocked_table_schema(table_id, table_schema))) {
      LOG_WARN("failed to get mocked table schema", K(table_id), K(ret));
    }
  } else {
    const uint64_t tenant_id = MTL_ID();
    OV (OB_NOT_NULL(schema_guard_));
    OZ (schema_guard_->get_table_schema(tenant_id, table_id, table_schema), table_id, is_link);
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(const uint64_t tenant_id,
                                      const uint64_t table_id,
                                      const share::schema::ObTableSchema *&table_schema,
                                      bool is_link /* = false*/)
{
  int ret = OB_SUCCESS;
  if (is_link) {
    OZ (get_link_table_schema(table_id, table_schema), table_id, is_link);
  } else if (is_external_object_id(table_id)) {
    if (OB_FAIL(get_mocked_table_schema(table_id, table_schema))) {
      LOG_WARN("failed to get mocked table schema", K(table_id), K(ret));
    }
  } else {
    OV (OB_NOT_NULL(schema_guard_));
    OZ (schema_guard_->get_table_schema(tenant_id, table_id, table_schema), table_id, is_link);
  }
  return ret;
}

int ObSqlSchemaGuard::get_database_schema(const uint64_t tenant_id,
                                          const uint64_t database_id,
                                          const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  database_schema = NULL;
  if (is_external_object_id(database_id)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < mocked_database_schemas_.count(); i++) {
      const share::schema::ObDatabaseSchema *&tmp_schema = mocked_database_schemas_.at(i);
      if (OB_ISNULL(tmp_schema)) {
        // 忽略本次 null，继续循环
        // ignore ret
        LOG_WARN("get unexpected null", K(ret));
      } else if (database_id == tmp_schema->get_database_id()) {
        database_schema = tmp_schema;
        break;
      }
    }

    // not found
    if (OB_SUCC(ret) && OB_ISNULL(database_schema)) {
      LOG_WARN("database not found", K(ret), K(database_id));
      ret = OB_ERR_BAD_DATABASE;
    }
  } else {
    OV(OB_NOT_NULL(schema_guard_));
    OZ(schema_guard_->get_database_schema(tenant_id, database_id, database_schema), tenant_id, database_id);
  }
  return ret;
}

int ObSqlSchemaGuard::get_database_schema(const uint64_t database_id,
                                          const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  if (OB_FAIL(get_database_schema(tenant_id, database_id, database_schema))) {
    LOG_WARN("failed to get database schema", K(ret), K(tenant_id), K(database_id));
  }
  return ret;
}

int ObSqlSchemaGuard::get_catalog_database_schema(const uint64_t tenant_id,
                                                  const uint64_t catalog_id,
                                                  const ObString &database_name,
                                                  const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  database_schema = NULL;
  if (OB_ISNULL(schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(schema_guard_->get_tenant_name_case_mode(tenant_id, case_mode))) {
    LOG_WARN("failed to get case mode", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < mocked_database_schemas_.count(); i++) {
      const share::schema::ObDatabaseSchema *&tmp_schema = mocked_database_schemas_.at(i);
      if (OB_ISNULL(tmp_schema)) {
        // 忽略本次 null，继续循环
        // ignore ret
        LOG_WARN("get unexpected null", K(ret));
      } else if (tenant_id == tmp_schema->get_tenant_id() && catalog_id == tmp_schema->get_catalog_id()
                 && ObCharset::case_mode_equal(case_mode, database_name, tmp_schema->get_database_name())) {
        database_schema = tmp_schema;
        break;
      }
    }
  }

  if (OB_SUCC(ret) && OB_ISNULL(database_schema)) {
    // not found from local, find from catalog and push into catalog_database_schemas_
    ObDatabaseSchema tmp_schema;
    ObCachedCatalogMetaGetter catalog_meta_getter{*schema_guard_, allocator_};
    // assign database id first
    tmp_schema.set_database_id(get_next_mocked_schema_id());
    if (OB_FAIL(catalog_meta_getter.fetch_namespace_schema(tenant_id, catalog_id, database_name, case_mode, tmp_schema))) {
      LOG_WARN("failed to fetch_namespace_schema", K(ret));
    } else if (OB_FAIL(add_mocked_database_schema(tmp_schema))) {
      LOG_WARN("failed to add_mocked_schema", K(ret));
    } else {
      // retrieve ObDatabaseSchema from mocked_database_schemas_
      database_schema = mocked_database_schemas_.at(mocked_database_schemas_.count() - 1);
    }
  }

  return ret;
}

int ObSqlSchemaGuard::get_catalog_database_id(const uint64_t tenant_id,
                                              const uint64_t catalog_id,
                                              const ObString &database_name,
                                              uint64_t &database_id)
{
  int ret = OB_SUCCESS;
  database_id = OB_INVALID_ID;
  const ObDatabaseSchema *schema = NULL;
  if (OB_FAIL(get_catalog_database_schema(tenant_id, catalog_id, database_name, schema))) {
    LOG_WARN("failed to get_catalog_database_schema", K(ret));
  } else if (OB_ISNULL(schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema must not be null", K(ret));
  } else {
    database_id = schema->get_database_id();
  }
  return ret;
}

int ObSqlSchemaGuard::get_catalog_table_schema(const uint64_t tenant_id,
                                               const uint64_t catalog_id,
                                               const uint64_t database_id,
                                               const ObString &database_name,
                                               const ObString &tbl_name,
                                               const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  table_schema = NULL;
  if (OB_ISNULL(schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(schema_guard_->get_tenant_name_case_mode(tenant_id, case_mode))) {
    LOG_WARN("failed to get case mode", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas_.count(); i++) {
      const ObTableSchema *&tmp_schema = table_schemas_.at(i);
      if (OB_ISNULL(tmp_schema)) {
        // 忽略本次 null，继续循环
        // ignore ret
        LOG_WARN("get unexpected null", K(ret));
      } else if (tenant_id == tmp_schema->get_tenant_id() && catalog_id == tmp_schema->get_catalog_id()
                 && database_id == tmp_schema->get_database_id()
                 && ObCharset::case_mode_equal(case_mode, tbl_name, tmp_schema->get_table_name())) {
        table_schema = tmp_schema;
        break;
      }
    }
  }

  if (OB_SUCC(ret) && OB_ISNULL(table_schema)) {
    // not found local, fetch from remote
    ObTableSchema tmp_schema;
    int64_t schema_version = 0;
    ObCachedCatalogMetaGetter catalog_meta_getter{*schema_guard_, allocator_};
    tmp_schema.set_database_id(database_id);
    tmp_schema.set_table_id(get_next_mocked_schema_id());
    if (OB_FAIL(catalog_meta_getter.fetch_table_schema(tenant_id, catalog_id, database_name, tbl_name, case_mode, tmp_schema))) {
      LOG_WARN("failed to fetch_table_schema", K(ret));
    } else if (OB_FAIL(schema_guard_->get_schema_version(tenant_id, schema_version))) {
      LOG_WARN("get schema version failed", K(ret));
    } else if (FALSE_IT(tmp_schema.set_schema_version(schema_version))) {
    } else if (OB_FAIL(add_mocked_table_schema(tmp_schema))) {
      LOG_WARN("add mocked table schema failed", K(ret));
    } else {
      table_schema = table_schemas_.at(table_schemas_.count() - 1);
    }
  }

  return ret;
}

int ObSqlSchemaGuard::get_catalog_table_id(const uint64_t tenant_id,
                                           const uint64_t catalog_id,
                                           const uint64_t database_id,
                                           const ObString &tbl_name,
                                           uint64_t &table_id)
{
  int ret = OB_SUCCESS;
  table_id = OB_INVALID_ID;
  const ObTableSchema *table_schema = NULL;
  if (OB_FAIL(get_catalog_table_schema(tenant_id, catalog_id, database_id, tbl_name, table_schema))) {
    LOG_WARN("get_catalog_table_schema failed", K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FALSE_IT(table_id = table_schema->get_table_id())) {
  }
  return ret;
}

int ObSqlSchemaGuard::get_catalog_table_schema(const uint64_t tenant_id,
                                               const uint64_t catalog_id,
                                               const uint64_t database_id,
                                               const ObString &tbl_name,
                                               const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema *database_schema = NULL;
  table_schema = NULL;
  if (OB_FAIL(get_database_schema(database_id, database_schema))) {
    LOG_WARN("get database schema failed", K(ret));
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get database schema failed", K(ret));
  } else if (OB_FAIL(get_catalog_table_schema(tenant_id,
                                              catalog_id,
                                              database_id,
                                              database_schema->get_database_name(),
                                              tbl_name,
                                              table_schema))) {
    LOG_WARN("get table schema failed", K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get table schema failed", K(ret));
  }
  return ret;
}

int ObSqlSchemaGuard::get_column_schema(uint64_t table_id, const ObString &column_name,
                                          const ObColumnSchemaV2 *&column_schema,
                                          bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  if (is_link) {
    OZ (get_link_column_schema(table_id, column_name, column_schema),
        table_id, column_name, is_link);
  } else if (is_external_object_id(table_id)) {
    const ObTableSchema *table_schema = NULL;
    OZ (get_mocked_table_schema(table_id, table_schema));
    if (OB_NOT_NULL(table_schema)) {
      column_schema = table_schema->get_column_schema(column_name);
    }
  } else {
    // first get table_schema, than try mock column_schema for part id
    const uint64_t tenant_id = MTL_ID();
    const ObTableSchema *table_schema = NULL;
    OV (OB_NOT_NULL(schema_guard_));
    OV ((OB_INVALID_ID != table_id && !column_name.empty()));
    OZ (schema_guard_->get_table_schema(tenant_id, table_id, table_schema));
    if (table_schema == NULL) {
      // do nothing, same as schema_guard_->get_column_schema()
    } else {
      OZ (sql::ObSQLMockSchemaUtils::try_mock_partid(table_schema, table_schema));
      OX (column_schema = table_schema->get_column_schema(column_name));
    }
  }
  return ret;
}

int ObSqlSchemaGuard::get_column_schema(uint64_t table_id, uint64_t column_id,
                                          const ObColumnSchemaV2 *&column_schema,
                                          bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  if (is_link) {
    OZ (get_link_column_schema(table_id, column_id, column_schema),
        table_id, column_id, is_link);
  } else if (is_external_object_id(table_id)) {
    const ObTableSchema *table_schema = NULL;
    OZ (get_mocked_table_schema(table_id, table_schema));
    if (OB_NOT_NULL(table_schema)) {
      column_schema = table_schema->get_column_schema(column_id);
    }
  } else {
    // first get table_schema, than try mock column_schema for part id
    const uint64_t tenant_id = MTL_ID();
    const ObTableSchema *table_schema = NULL;
    OV (OB_NOT_NULL(schema_guard_));
    OV ((OB_INVALID_ID != table_id && OB_INVALID_ID != column_id));
    OZ (schema_guard_->get_table_schema(tenant_id, table_id, table_schema));
    if (table_schema == NULL) {
      // do nothing, same as schema_guard_->get_column_schema()
    } else {
      OZ (sql::ObSQLMockSchemaUtils::try_mock_partid(table_schema, table_schema));
      OX (column_schema = table_schema->get_column_schema(column_id));
    }
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema_version(const uint64_t table_id,
                                               int64_t &schema_version) const
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  OV (OB_NOT_NULL(schema_guard_));
  OZ (schema_guard_->get_schema_version(TABLE_SCHEMA, tenant_id, table_id, schema_version), table_id);
  return ret;
}

int ObSqlSchemaGuard::get_can_read_index_array(uint64_t table_id,
                                                 uint64_t *index_tid_array,
                                                 int64_t &size,
                                                 bool with_mv,
                                                 bool with_global_index,
                                                 bool with_domain_index,
                                                 bool with_spatial_index,
                                                 bool with_vector_index)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  if (is_external_object_id(table_id)) {
    size = 0;
  } else {
    OV (OB_NOT_NULL(schema_guard_));
    OZ (schema_guard_->get_can_read_index_array(tenant_id, table_id,
                                                index_tid_array, size, with_mv,
                                                with_global_index, with_domain_index,
                                                with_spatial_index, with_vector_index));
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_mlog_schema(const uint64_t table_id,
                                            const ObTableSchema *&mlog_schema)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  OV (OB_NOT_NULL(schema_guard_));
  OZ (schema_guard_->get_table_mlog_schema(tenant_id, table_id, mlog_schema));
  return ret;
}

int ObSqlSchemaGuard::get_link_table_schema(uint64_t table_id,
                                              const ObTableSchema *&table_schema) const
{
  int ret = OB_SUCCESS;
  int64_t schema_count = table_schemas_.count();
  const ObTableSchema *tmp_schema = NULL;
  table_schema = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && OB_ISNULL(table_schema) && i < schema_count; i++) {
    OX (tmp_schema = table_schemas_.at(i));
    OV (OB_NOT_NULL(tmp_schema));
    if (OB_SUCC(ret) && table_id == tmp_schema->get_table_id()) {
      table_schema = tmp_schema;
    }
  }
  return ret;
}

int ObSqlSchemaGuard::get_link_column_schema(uint64_t table_id, const ObString &column_name,
                                               const ObColumnSchemaV2 *&column_schema) const
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  OZ (get_link_table_schema(table_id, table_schema), table_id);
  if (OB_NOT_NULL(table_schema)) {
    OX (column_schema = table_schema->get_column_schema(column_name));
  }
  return ret;
}

int ObSqlSchemaGuard::get_link_column_schema(uint64_t table_id, uint64_t column_id,
                                               const ObColumnSchemaV2 *&column_schema) const
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  OZ (get_link_table_schema(table_id, table_schema), table_id);
  if (OB_NOT_NULL(table_schema)) {
    OX (column_schema = table_schema->get_column_schema(column_id));
  }
  return ret;
}

int ObSqlSchemaGuard::get_link_current_scn(uint64_t dblink_id, uint64_t tenant_id,
                                           ObSQLSessionInfo *session_info,
                                           uint64_t &current_scn)
{
  int ret = OB_SUCCESS;
  current_scn = OB_INVALID_ID;
  if (!dblink_scn_.created()) {
    if (OB_FAIL(dblink_scn_.create(4, "DblinkScnMap", "DblinkScnMap", tenant_id))) {
      LOG_WARN("create hash map failed", K(ret));
    } else {
      ret = OB_HASH_NOT_EXIST;
    }
  } else {
    if (OB_FAIL(dblink_scn_.get_refactored(dblink_id, current_scn))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("get dblink scn failed", K(ret));
      }
    }
  }
  return ret;
}

common::ObIArray<const share::schema::ObDatabaseSchema *> &ObSqlSchemaGuard::get_mocked_database_schemas()
{
  return mocked_database_schemas_;
}

common::ObIArray<const share::schema::ObTableSchema *> &ObSqlSchemaGuard::get_mocked_table_schemas()
{
  return table_schemas_;
}

int ObSqlCtx::set_partition_infos(const ObTablePartitionInfoArray &info, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  int64_t count = info.count();
  partition_infos_.reset();
  if (count > 0) {
    partition_infos_.set_allocator(&allocator);
    if (OB_FAIL(partition_infos_.init(count))) {
      LOG_WARN("init partition info failed", K(ret), K(count));
    } else {
      for (int64_t i = 0; i < count && OB_SUCC(ret); ++i) {
        if (OB_FAIL(partition_infos_.push_back(info.at(i)))) {
          LOG_WARN("push partition info failed", K(ret), K(count));
        }
      }
    }
  }
  return ret;
}

int ObSqlCtx::set_related_user_var_names(const ObIArray<ObString> &user_var_names,
                                         ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (user_var_names.count() > 0) {
    related_user_var_names_.reset();
    related_user_var_names_.set_allocator(&allocator);
    if (OB_FAIL(related_user_var_names_.init(user_var_names.count()))) {
      LOG_WARN("failed to init related_user_var_names", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < user_var_names.count(); i++) {
        if (OB_FAIL(related_user_var_names_.push_back(user_var_names.at(i)))) {
          LOG_WARN("failed to push back user var names", K(ret));
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    related_user_var_names_.reset();
  }
  return ret;
}

int ObSqlCtx::set_location_constraints(const ObLocationConstraintContext &location_constraint,
                                       ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  base_constraints_.reset();
  strict_constraints_.reset();
  non_strict_constraints_.reset();
  dup_table_replica_cons_.reset();
  const ObIArray<LocationConstraint> &base_constraints = location_constraint.base_table_constraints_;
  const ObIArray<ObPwjConstraint *> &strict_constraints = location_constraint.strict_constraints_;
  const ObIArray<ObPwjConstraint *> &non_strict_constraints = location_constraint.non_strict_constraints_;
  const ObIArray<ObDupTabConstraint> &dup_table_replica_cons = location_constraint.dup_table_replica_cons_;
  if (base_constraints.count() > 0) {
    base_constraints_.set_allocator(&allocator);
    if (OB_FAIL(base_constraints_.init(base_constraints.count()))) {
      LOG_WARN("init base constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < base_constraints.count(); i++) {
        if (OB_FAIL(base_constraints_.push_back(base_constraints.at(i)))) {
          LOG_WARN("failed to push back base constraint", K(ret));
        } else {
          // table_partition_info_仅在计划生成阶段使用
          base_constraints_.at(i).table_partition_info_ = NULL;
        }
      }
      LOG_DEBUG("set base constraints", K(base_constraints.count()));
    }
  }
  if (OB_SUCC(ret) && strict_constraints.count() > 0) {
    strict_constraints_.set_allocator(&allocator);
    if (OB_FAIL(strict_constraints_.init(strict_constraints.count()))) {
      LOG_WARN("init strict constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < strict_constraints.count(); i++) {
        if (OB_FAIL(strict_constraints_.push_back(strict_constraints.at(i)))) {
          LOG_WARN("failed to push back location constraint", K(ret));
        }
      }
      LOG_DEBUG("set strict constraints", K(strict_constraints.count()));
    }
  }
  if (OB_SUCC(ret) && non_strict_constraints.count() > 0) {
    non_strict_constraints_.set_allocator(&allocator);
    if (OB_FAIL(non_strict_constraints_.init(non_strict_constraints.count()))) {
      LOG_WARN("init non strict constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < non_strict_constraints.count(); i++) {
        if (OB_FAIL(non_strict_constraints_.push_back(non_strict_constraints.at(i)))) {
          LOG_WARN("failed to push back location constraint", K(ret));
        }
      }
      LOG_DEBUG("set non strict constraints", K(non_strict_constraints.count()));
    }
  }
  if (OB_SUCC(ret) && dup_table_replica_cons.count() > 0) {
    dup_table_replica_cons_.set_allocator(&allocator);
    if (OB_FAIL(dup_table_replica_cons_.init(dup_table_replica_cons.count()))) {
      LOG_WARN("init duplicate table replica constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < dup_table_replica_cons.count(); i++) {
        if (OB_FAIL(dup_table_replica_cons_.push_back(dup_table_replica_cons.at(i)))) {
          LOG_WARN("failed to push back location constraint", K(ret));
        }
      }
      LOG_DEBUG("set duplicate table replica constraints", K(dup_table_replica_cons.count()));
    }
  }
  return ret;
}

int ObSqlCtx::set_multi_stmt_rowkey_pos(const common::ObIArray<int64_t> &multi_stmt_rowkey_pos,
                                        common::ObIAllocator &alloctor)
{
  int ret = OB_SUCCESS;
  if (!multi_stmt_rowkey_pos.empty()) {
    multi_stmt_rowkey_pos_.set_allocator(&alloctor);
    if (OB_FAIL(multi_stmt_rowkey_pos_.init(multi_stmt_rowkey_pos.count()))) {
      LOG_WARN("failed to init rowkey count", K(ret));
    } else if (OB_FAIL(append(multi_stmt_rowkey_pos_, multi_stmt_rowkey_pos))) {
      LOG_WARN("failed to append multi stmt rowkey pos", K(ret));
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObQueryCtx::add_local_session_vars(ObIAllocator *alloc, const ObLocalSessionVar &local_session_var, int64_t &idx) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(all_local_session_vars_.push_back(ObLocalSessionVar()))) {
    LOG_WARN("push back local session var failed", K(ret));
  } else {
    idx = all_local_session_vars_.count() - 1;
    ObLocalSessionVar &local_var = all_local_session_vars_.at(idx);
    local_var.set_allocator(alloc);
    if (OB_FAIL(local_var.deep_copy(local_session_var))) {
      LOG_WARN("deep copy local session var failed", K(ret));
    }
  }
  return ret;
}

int ObQueryCtx::get_local_session_vars(const int64_t idx, const ObLocalSessionVar *&local_session_var) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= all_local_session_vars_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid idx", K(ret), K(idx), K(all_local_session_vars_.count()));
  } else {
    local_session_var = &all_local_session_vars_.at(idx);
  }
  return ret;
}

}
}
