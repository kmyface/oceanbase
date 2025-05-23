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

#ifndef OCEANBASE_LOCK_OB_SCOND_H_
#define OCEANBASE_LOCK_OB_SCOND_H_
#include "lib/ob_define.h"
#include "lib/thread_local/ob_tsi_utils.h"
#include "lib/lock/ob_futex.h"
#include "lib/wait_event/ob_wait_event.h"
#include "lib/stat/ob_diagnose_info.h"

namespace oceanbase
{
namespace common
{
struct SimpleCond
{
public:
  SimpleCond(): n_waiters_(0), event_no_(ObWaitEventIds::DEFAULT_COND_WAIT) {}
  SimpleCond(int32_t event_no): n_waiters_(0), event_no_(event_no) {}
  ~SimpleCond() {}

  void init(int32_t event_no) { event_no_ = event_no; }
  uint32_t get_key() { return ATOMIC_LOAD(&futex_.uval()); }
  int wait(uint32_t key, int64_t timeout) {
    int ret = OB_SUCCESS;
    if (timeout > 0 && get_key() == key) {
      if (ObWaitEventIds::DEFAULT_COND_WAIT != event_no_) {
        ObWaitEventGuard guard(event_no_, timeout / 1000, reinterpret_cast<int64_t>(this), 0, 0, true);
        ATOMIC_FAA(&n_waiters_, 1);
        ret = futex_.wait(key, timeout);
        ATOMIC_FAA(&n_waiters_, -1);
      } else {
        ATOMIC_FAA(&n_waiters_, 1);
        ret = futex_.wait(key, timeout);
        ATOMIC_FAA(&n_waiters_, -1);
      }
    }

    return ret;
  }

  uint32_t signal(uint32_t limit = 1) {
    uint32_t n2wakeup = 0;
    ATOMIC_FAA(&futex_.uval(), 1);
    if (ATOMIC_LOAD(&n_waiters_) > 0) {
      n2wakeup = (uint32_t)futex_.wake(limit);
    }
    return n2wakeup;
  }
private:
  lib::ObFutex futex_;
  uint32_t n_waiters_;
  int32_t event_no_;
};

struct SCondReadyFlag
{
public:
  SCondReadyFlag(): lock_(0) {}
  ~SCondReadyFlag() {}
  bool trylock() {
    bool bool_ret = false;
    int value = ATOMIC_LOAD(&lock_);
    if (value <= 0) {
      if (ATOMIC_BCAS(&lock_, value, 1)) {
        bool_ret = true;
      }
    }
    return bool_ret;
  }
  void unlock() { ATOMIC_STORE(&lock_, 0); }
private:
  int lock_;
} CACHE_ALIGNED;

class SCondSimpleCounter
{
public:
  SCondSimpleCounter(): count_(0) {}
  void add(uint32_t x) { ATOMIC_FAA(&count_, x); }
  uint32_t fetch() {
    int64_t sum = ATOMIC_TAS(&count_, 0);
    return (uint32_t)std::min(sum, 65536L);
  }
private:
  int64_t count_;
} CACHE_ALIGNED;

class SCondCounter
{
public:
  enum { CPU_COUNT = OB_MAX_CPU_NUM };
  struct Item
  {
    Item(): count_(0) {}
    int64_t count_ CACHE_ALIGNED;
  };
  SCondCounter() {}
  void add(uint32_t x, uint32_t icpu_id) { ATOMIC_FAA(&count_[icpu_id % CPU_COUNT].count_, x); }
  uint32_t fetch() {
    int64_t sum = 0;
    for(int i = 0; i < CPU_COUNT; i++) {
      sum += ATOMIC_TAS(&count_[i].count_, 0);
    }
    return (uint32_t)std::min(sum, 65536L);
  }
private:
  Item count_[CPU_COUNT];
};

class SCondSimpleIdGen
{
public:
  uint32_t next() { return (uint32_t)icpu_id(); }
  uint32_t get() { return (uint32_t)icpu_id(); }
};

template <int PRIO>
struct SCondTemp
{
public:
  typedef SimpleCond CondPerCpu;
  typedef SCondReadyFlag Lock;
  typedef SCondCounter Counter;
  typedef SCondSimpleIdGen IdGen;
  enum { CPU_COUNT = OB_MAX_CPU_NUM, COND_COUNT = CPU_COUNT, LOOP_LIMIT = 8 };
  void signal(uint32_t x = 1, int prio=0) {
    uint32_t icpu_id = id_gen_.get();
    for (int p = PRIO-1; p >= prio && x > 0; p--) {
      x -= conds_[icpu_id % COND_COUNT][p].signal(x);
    }
    if (x > 0) {
      n2wakeup_.add(x, icpu_id);
      int64_t loop_cnt = 0;
      while(loop_cnt < LOOP_LIMIT) {
        if (lock_.trylock()) {
          do_wakeup();
          lock_.unlock();
          break;
        } else {
          PAUSE();
          loop_cnt++;
        }
      }
      if (loop_cnt > LOOP_LIMIT) {
        do_wakeup();
      }
    }
  }
  void prepare(int prio=0) {
    uint32_t id = 0;
    uint32_t key = get_key(prio, id);
    id += (prio << 16);
    get_wait_key() = ((uint64_t)id<<32) + key;
  }
  int wait(int64_t timeout){
    uint64_t wait_key = get_wait_key();
    return wait((uint32_t)(wait_key>>32), (uint32_t)wait_key, timeout);
  }
protected:
  uint32_t get_key(int prio, uint32_t& id) { return conds_[id = (id_gen_.next() % COND_COUNT)][prio].get_key(); }
  int wait(uint32_t id, uint32_t key, int64_t timeout) {
    return conds_[((uint16_t)id) % COND_COUNT][id >> 16].wait(key, timeout);
  }
private:
  static uint64_t& get_wait_key() {
    RLOCAL(uint64_t, key);
    return key;
  }
  void do_wakeup() {
    uint32_t n2wakeup = 0;
    //for (int p = PRIO - 1; p >= 0; p--) {
      n2wakeup = n2wakeup_.fetch();
      //    }
    for (int p = PRIO - 1; n2wakeup > 0 && p >= 0; p--) {
      for(int i = 0; n2wakeup > 0 && i < COND_COUNT; i++) {
        n2wakeup -= conds_[i][p].signal(n2wakeup);
      }
    }
  }
private:
  Lock lock_ CACHE_ALIGNED;
  CondPerCpu conds_[COND_COUNT][PRIO];
  Counter n2wakeup_;
  IdGen id_gen_;
};

using SCond = SCondTemp<1>;

}; // end namespace common
}; // end namespace oceanbase

#endif /* OCEANBASE_LOCK_OB_SCOND_H_ */
