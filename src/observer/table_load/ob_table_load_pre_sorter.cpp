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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_pre_sorter.h"
#include "observer/table_load/ob_table_load_mem_chunk_manager.h"
#include "observer/table_load/ob_table_load_service.h"
#include "observer/table_load/ob_table_load_store_ctx.h"
#include "observer/table_load/ob_table_load_store_table_ctx.h"
#include "observer/table_load/ob_table_load_table_ctx.h"
#include "observer/table_load/ob_table_load_task.h"
#include "observer/table_load/ob_table_load_task_scheduler.h"
#include "storage/direct_load/ob_direct_load_mem_sample.h"
#include "storage/direct_load/ob_direct_load_table_store.h"
#include "storage/direct_load/ob_direct_load_mem_worker.h"
namespace oceanbase
{
namespace observer
{
using namespace storage;
using namespace common;

class ObTableLoadPreSorter::ChunkSorter : public ObDirectLoadMemWorker
{
  using ChunkType = storage::ObDirectLoadExternalMultiPartitionRowChunk;
  using CompareType = storage::ObDirectLoadExternalMultiPartitionRowCompare;
public:
  ChunkSorter(ObDirectLoadMemContext *mem_ctx,
              ObTableLoadPreSorter *pre_sorter,
              ObTableLoadMemChunkManager *chunk_mgr,
              int64_t chunk_node_id);
  virtual ~ChunkSorter();
  virtual int add_table(const ObDirectLoadTableHandle &table_handle) override { return OB_SUCCESS; }
  virtual int work() override;
  VIRTUAL_TO_STRING_KV(KP(mem_ctx_), K_(chunk_node_id));
private:
  ObDirectLoadMemContext *mem_ctx_;
  observer::ObTableLoadPreSorter *pre_sorter_;
  observer::ObTableLoadMemChunkManager *chunk_mgr_;
  int64_t chunk_node_id_;
};

ObTableLoadPreSorter::ChunkSorter::ChunkSorter(ObDirectLoadMemContext *mem_ctx,
                                               ObTableLoadPreSorter *pre_sorter,
                                               ObTableLoadMemChunkManager *chunk_mgr,
                                               int64_t chunk_node_id)
  : mem_ctx_(mem_ctx),
    pre_sorter_(pre_sorter),
    chunk_mgr_(chunk_mgr),
    chunk_node_id_(chunk_node_id)
{
  pre_sorter_->inc_sort_chunk_task_cnt();

}

ObTableLoadPreSorter::ChunkSorter::~ChunkSorter()
{
}

int ObTableLoadPreSorter::ChunkSorter::work()
{
  int ret = OB_SUCCESS;
  CompareType compare;
  if (OB_ISNULL(chunk_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("chunk mgr is nullptr", KR(ret));
  } else if (OB_FAIL(chunk_mgr_->close_chunk(chunk_node_id_))) {
    LOG_WARN("fail to sort chunk", KR(ret), K(chunk_node_id_));
  } else {
    bool all_trans_finished = ATOMIC_LOAD(&pre_sorter_->all_trans_finished_);
    int64_t cur_sort_chunk_task_cnt = pre_sorter_->dec_sort_chunk_task_cnt();
    if (all_trans_finished && 0 == cur_sort_chunk_task_cnt) {
      mem_ctx_->load_thread_cnt_ = 0; // 用于让sample线程退出
    }
  }
  return ret;
}

ObTableLoadPreSorter::ObTableLoadPreSorter(ObTableLoadStoreCtx *store_ctx)
  : ctx_(store_ctx->ctx_),
    store_ctx_(store_ctx),
    allocator_("TLD_PreSorter"),
    chunks_manager_(nullptr),
    sample_task_scheduler_(nullptr),
    unclosed_chunk_id_pos_(0),
    finish_thread_cnt_(0),
    sort_chunk_task_cnt_(0),
    all_trans_finished_(false),
    is_inited_(false)
{
  allocator_.set_tenant_id(MTL_ID());
}

ObTableLoadPreSorter::~ObTableLoadPreSorter()
{
  reset();
}

void ObTableLoadPreSorter::reset()
{
  is_inited_ = false;
  // 先把sample线程停下来
  mem_ctx_.has_error_ = true;
  if (OB_NOT_NULL(sample_task_scheduler_)) {
    sample_task_scheduler_->stop();
    sample_task_scheduler_->wait();
    sample_task_scheduler_->~ObITableLoadTaskScheduler();
    allocator_.free(sample_task_scheduler_);
    sample_task_scheduler_ = nullptr;
  }
  ctx_ = nullptr;
  store_ctx_ = nullptr;
  mem_ctx_.reset();
  if (nullptr != chunks_manager_) {
    chunks_manager_->~ObTableLoadMemChunkManager();
    allocator_.free(chunks_manager_);
    chunks_manager_ = nullptr;
  }
  unclosed_chunk_id_pos_ = 0;
  finish_thread_cnt_ = 0;
  sort_chunk_task_cnt_ = 0;
  all_trans_finished_ = false;
  // 分配器最后reset
  allocator_.reset();
}

int ObTableLoadPreSorter::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableLoadPreSorter init twice", KR(ret), KP(this));
  } else {
    if (OB_FAIL(init_mem_ctx())) {
      LOG_WARN("fail to init mem ctx", KR(ret));
    } else if (OB_FAIL(init_chunks_manager())) {
      LOG_WARN("fail to init chunks manager", KR(ret));
    } else if (OB_FAIL(init_sample_task_scheduler())) {
      LOG_WARN("fail to init sample task scheduler", KR(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTableLoadPreSorter::init_mem_ctx()
{
  int ret = OB_SUCCESS;
  mem_ctx_.table_data_desc_ = store_ctx_->write_ctx_.table_data_desc_;
  mem_ctx_.datum_utils_ = &(ctx_->schema_.datum_utils_);
  mem_ctx_.dml_row_handler_ = store_ctx_->write_ctx_.dml_row_handler_;
  mem_ctx_.file_mgr_ = store_ctx_->tmp_file_mgr_;
  mem_ctx_.table_mgr_ = store_ctx_->table_mgr_;
  mem_ctx_.dup_action_ = ctx_->param_.dup_action_;

  mem_ctx_.exe_mode_ = ctx_->param_.exe_mode_;
  mem_ctx_.merge_count_per_round_ = store_ctx_->merge_count_per_round_;
  mem_ctx_.max_mem_chunk_count_ = store_ctx_->max_mem_chunk_count_;
  mem_ctx_.mem_chunk_size_ = store_ctx_->mem_chunk_size_;
  mem_ctx_.heap_table_mem_chunk_size_ = store_ctx_->heap_table_mem_chunk_size_;

  mem_ctx_.total_thread_cnt_ = store_ctx_->thread_cnt_;
  mem_ctx_.dump_thread_cnt_ = mem_ctx_.total_thread_cnt_;
  mem_ctx_.load_thread_cnt_ = mem_ctx_.total_thread_cnt_;
  if (OB_FAIL(mem_ctx_.init())) {
    LOG_WARN("fail to init mem ctx", KR(ret));
  }
  return ret;
}

int ObTableLoadPreSorter::init_chunks_manager()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(chunks_manager_ = OB_NEWx(ObTableLoadMemChunkManager, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new chunks_manager_", KR(ret));
  } else if (OB_FAIL(chunks_manager_->init(store_ctx_, &mem_ctx_))) {
    LOG_WARN("fail to init chunks manager", KR(ret));
  }
  return ret;
}

int ObTableLoadPreSorter::init_sample_task_scheduler()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sample_task_scheduler_ =
                  OB_NEWx(ObTableLoadTaskThreadPoolScheduler, &allocator_, 1,
                          ctx_->param_.table_id_, "MemSample", ctx_->session_info_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new sample task scheduler", KR(ret));
  } else if (OB_FAIL(sample_task_scheduler_->init())) {
    LOG_WARN("fail to init sample task scheduler", KR(ret));
  } else if (OB_FAIL(sample_task_scheduler_->start())) {
    LOG_WARN("fail to start sample task scheduler", KR(ret));
  }
  return ret;
}

int ObTableLoadPreSorter::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPreSorter not init", KR(ret));
  } else if (OB_FAIL(start_sample())) {
    LOG_WARN("fail to start sample", KR(ret));
  } else if (OB_FAIL(start_dump())) {
    LOG_WARN("fail to start dump", KR(ret));
  }
  return ret;
}

int ObTableLoadPreSorter::close()
{
  int ret = OB_SUCCESS;
  ObArray<int64_t> unclosed_chunk_ids;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPreSorter not init", KR(ret));
  } else if (OB_FAIL(chunks_manager_->get_unclosed_chunks(unclosed_chunk_ids))) {
    LOG_WARN("fail to get unclosed chunks", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < unclosed_chunk_ids.count(); i++) {
      if (OB_FAIL(close_chunk(unclosed_chunk_ids.at(i)))) {
        LOG_WARN("fail to close chunk", KR(ret), K(i),K(unclosed_chunk_ids.at(i)));
      }
    }
  }
  if (OB_SUCC(ret)) {
    all_trans_finished_ = true;
    if (0 == get_sort_chunk_task_cnt()) {
      mem_ctx_.load_thread_cnt_ = 0;
    }
  }
  return ret;
}

int ObTableLoadPreSorter::get_table_store(ObDirectLoadTableStore &table_store)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPreSorter not init", KR(ret));
  } else {
    table_store.clear();
    table_store.set_table_data_desc(mem_ctx_.table_data_desc_);
    table_store.set_multiple_sstable();
    if (OB_FAIL(table_store.add_tables(mem_ctx_.tables_handle_))) {
      LOG_WARN("fail to add tables", KR(ret));
    }
  }
  return ret;
}

int ObTableLoadPreSorter::close_chunk(int64_t chunk_node_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPreSorter is not init", KR(ret));
  } else {
    ObMemAttr mem_attr(MTL_ID(), "TLD_ChunkSort");
    ChunkSorter *chunk_sorter = nullptr;
    if (OB_ISNULL(chunk_sorter = OB_NEW(ChunkSorter,
                                        mem_attr,
                                        &mem_ctx_,
                                        this,
                                        chunks_manager_,
                                        chunk_node_id))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ChunkSorter", KR(ret));
    } else if (OB_FAIL(mem_ctx_.mem_dump_queue_.push(chunk_sorter))) {
      LOG_WARN("fail to push sort chunk task", KR(ret));
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(chunk_sorter)) {
      chunk_sorter->~ChunkSorter();
      ob_free(chunk_sorter);
    }
  }
  return ret;
}

int ObTableLoadPreSorter::start_sample()
{
  int ret = OB_SUCCESS;
  ObTableLoadTask *task = nullptr;
  if (OB_FAIL(ctx_->alloc_task(task))) {
    LOG_WARN("fail to alloc task", KR(ret));
  } else if (OB_FAIL(task->set_processor<SampleTaskProcessor>(ctx_, &mem_ctx_))) {
    LOG_WARN("fail to set sample task processor", KR(ret));
  } else if (OB_FAIL(task->set_callback<PreSortTaskCallback>(ctx_, this))) {
    LOG_WARN("fail to set pre sort task callback", KR(ret));
  } else if (OB_FAIL(sample_task_scheduler_->add_task(0, task))) {
    LOG_WARN("fail to add task", KR(ret), KPC(task));
  }
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(task)) {
      ctx_->free_task(task);
    }
  }
  return ret;
}

int ObTableLoadPreSorter::start_dump()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < mem_ctx_.dump_thread_cnt_; ++i) {
    ObTableLoadTask *task = nullptr;
    if (OB_FAIL(ctx_->alloc_task(task))) {
      LOG_WARN("fail to alloc task", KR(ret));
    } else if (OB_FAIL(task->set_processor<DumpTaskProcessor>(ctx_, &mem_ctx_))) {
      LOG_WARN("fail to set dump task processor", KR(ret));
    } else if (OB_FAIL(task->set_callback<PreSortTaskCallback>(ctx_, this))) {
      LOG_WARN("fail to set pre sort task callback", KR(ret));
    } else if (OB_FAIL(store_ctx_->task_scheduler_->add_task(i, task))) {
      LOG_WARN("fail to add task", KR(ret), KPC(task));
    }
    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(task)) {
        ctx_->free_task(task);
      }
    }
  }
  return ret;
}

int ObTableLoadPreSorter::start_finish()
{
  int ret = OB_SUCCESS;
  ObTableLoadTask *task = nullptr;
  // 1. 分配task
  if (OB_FAIL(ctx_->alloc_task(task))) {
    LOG_WARN("fail to alloc task", KR(ret));
  }
  // 2. 设置processor
  else if (OB_FAIL(task->set_processor<FinishTaskProcessor>(ctx_, this))) {
    LOG_WARN("fail to set finish task processor", KR(ret));
  }
  // 3. 设置callback
  else if (OB_FAIL(task->set_callback<FinishTaskCallback>(ctx_))) {
    LOG_WARN("fail to set finish task callback", KR(ret));
  }
  // 4. 把task放入调度器
  else if (OB_FAIL(store_ctx_->task_scheduler_->add_task(0, task))) {
    LOG_WARN("fail to add task", KR(ret), KPC(task));
  }
  if (OB_FAIL(ret)) {
    if (nullptr != task) {
      ctx_->free_task(task);
    }
  }
  return ret;
}

void ObTableLoadPreSorter::stop()
{
  set_has_error();
  if (OB_NOT_NULL(sample_task_scheduler_)) {
    sample_task_scheduler_->stop();
  }
}

void ObTableLoadPreSorter::wait()
{
  if (OB_NOT_NULL(sample_task_scheduler_)) {
    sample_task_scheduler_->wait();
  }
}

bool ObTableLoadPreSorter::is_stopped() const
{
  return (nullptr == sample_task_scheduler_ || sample_task_scheduler_->is_stopped());
}

int ObTableLoadPreSorter::handle_pre_sort_thread_finish()
{
  int ret = OB_SUCCESS;
  const int64_t finish_thread_cnt = ATOMIC_AAF(&finish_thread_cnt_, 1);
  if (finish_thread_cnt == mem_ctx_.total_thread_cnt_ + 1 /*sample thread*/) {
    // all tasks have done
    if (OB_LIKELY(!mem_ctx_.has_error_) && OB_FAIL(start_finish())) {
      LOG_WARN("fail to start finish", KR(ret));
    }
  }
  return ret;
}

int ObTableLoadPreSorter::finish()
{
  int ret = OB_SUCCESS;
  ObDirectLoadTableStore &table_store = store_ctx_->data_store_table_ctx_->insert_table_store_;
  table_store.clear();
  table_store.set_table_data_desc(mem_ctx_.table_data_desc_);
  table_store.set_multiple_sstable();
  if (OB_FAIL(table_store.add_tables(mem_ctx_.tables_handle_))) {
    LOG_WARN("fail to add tables", KR(ret));
  } else {
    mem_ctx_.reset();
    sample_task_scheduler_->stop();
    if (OB_FAIL(store_ctx_->handle_pre_sort_success())) {
      LOG_WARN("fail to handle pre sort success", KR(ret));
    }
  }
  return ret;
}

class ObTableLoadPreSorter::SampleTaskProcessor : public ObITableLoadTaskProcessor
{
public:
  SampleTaskProcessor(ObTableLoadTask &task,
                      ObTableLoadTableCtx *ctx,
                      ObDirectLoadMemContext *mem_ctx)
    : ObITableLoadTaskProcessor(task), ctx_(ctx), mem_ctx_(mem_ctx)
  {
    ctx_->inc_ref_count();
  }
  virtual ~SampleTaskProcessor()
  {
    ObTableLoadService::put_ctx(ctx_);
  }
  int process() override
  {
    int ret = OB_SUCCESS;
    ObDirectLoadMemSample sample(ctx_, mem_ctx_);
    if (OB_FAIL(sample.do_sample())) {
      LOG_WARN("fail to do sample", KR(ret));
    }
    return ret;
  }
private:
  ObTableLoadTableCtx *ctx_;
  ObDirectLoadMemContext *mem_ctx_;
};

class ObTableLoadPreSorter::DumpTaskProcessor : public ObITableLoadTaskProcessor
{
public:
  DumpTaskProcessor(ObTableLoadTask &task,
                    ObTableLoadTableCtx *ctx,
                    ObDirectLoadMemContext *mem_ctx)
    : ObITableLoadTaskProcessor(task), ctx_(ctx), mem_ctx_(mem_ctx)
  {
    ctx_->inc_ref_count();
  }
  virtual ~DumpTaskProcessor()
  {
    ObTableLoadService::put_ctx(ctx_);
  }
  int process() override
  {
    int ret = OB_SUCCESS;
    void *tmp = nullptr;
    while (OB_SUCC(ret) && OB_LIKELY(!mem_ctx_->has_error_)) {
      if (OB_FAIL(mem_ctx_->mem_dump_queue_.pop(tmp))) {
        LOG_WARN("fail to pop", KR(ret));
      } else {
        if (OB_NOT_NULL(tmp)) {
          ObDirectLoadMemWorker *worker = static_cast<ObDirectLoadMemWorker *>(tmp);
          if (OB_FAIL(worker->work())) {
            LOG_WARN("fail to do dump", KR(ret));
          }
          worker->~ObDirectLoadMemWorker();
          ob_free(worker);
        } else {
          if (OB_FAIL(mem_ctx_->mem_dump_queue_.push(nullptr))) {
            LOG_WARN("fail to push nullptr", KR(ret));
          } else {
            break;
          }
        }
      }
    }
    return ret;
  }
private:
  ObTableLoadTableCtx *ctx_;
  ObDirectLoadMemContext *mem_ctx_;
};

class ObTableLoadPreSorter::PreSortTaskCallback : public ObITableLoadTaskCallback
{
public:
  PreSortTaskCallback(ObTableLoadTableCtx *ctx, ObTableLoadPreSorter *pre_sorter)
    : ctx_(ctx), pre_sorter_(pre_sorter)
  {
    ctx_->inc_ref_count();
  }
  virtual ~PreSortTaskCallback()
  {
    ObTableLoadService::put_ctx(ctx_);
  }
  void callback(int ret_code, ObTableLoadTask *task) override
  {
    int ret = ret_code;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(pre_sorter_->handle_pre_sort_thread_finish())) {
        LOG_WARN("fail to handle pre sort thread finish", KR(ret));
      }
    }
    if (OB_FAIL(ret)) {
      pre_sorter_->set_has_error();
      ctx_->store_ctx_->set_status_error(ret);
    }
    ctx_->free_task(task);
    OB_TABLE_LOAD_STATISTICS_PRINT_AND_RESET();
  }
private:
  ObTableLoadTableCtx *ctx_;
  ObTableLoadPreSorter *pre_sorter_;
};

class ObTableLoadPreSorter::FinishTaskProcessor : public ObITableLoadTaskProcessor
{
public:
  FinishTaskProcessor(ObTableLoadTask &task,
                      ObTableLoadTableCtx *ctx,
                      ObTableLoadPreSorter *pre_sorter)
    : ObITableLoadTaskProcessor(task), ctx_(ctx), pre_sorter_(pre_sorter)
  {
    ctx_->inc_ref_count();
  }
  virtual ~FinishTaskProcessor()
  {
    ObTableLoadService::put_ctx(ctx_);
  }
  int process() override { return pre_sorter_->finish(); }
private:
  ObTableLoadTableCtx *const ctx_;
  ObTableLoadPreSorter *const pre_sorter_;
};

class ObTableLoadPreSorter::FinishTaskCallback : public ObITableLoadTaskCallback
{
public:
  FinishTaskCallback(ObTableLoadTableCtx *ctx) : ctx_(ctx) { ctx_->inc_ref_count(); }
  virtual ~FinishTaskCallback() { ObTableLoadService::put_ctx(ctx_); }
  void callback(int ret_code, ObTableLoadTask *task) override
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret_code)) {
      ctx_->store_ctx_->set_status_error(ret);
    }
    ctx_->free_task(task);
    OB_TABLE_LOAD_STATISTICS_PRINT_AND_RESET();
  }
private:
  ObTableLoadTableCtx *const ctx_;
};

} // namespace observer
} // namespace oceanbase
