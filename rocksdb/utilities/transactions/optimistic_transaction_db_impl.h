//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <vector>

#include "db/write_callback.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/utilities/optimistic_transaction_db.h"
#include "util/cast_util.h"
#include "util/mutexlock.h"
#include "utilities/transactions/optimistic_transaction.h"

namespace ROCKSDB_NAMESPACE {

class OccLockBucketsImplBase : public OccLockBuckets {
 public:
  virtual port::Mutex& GetLockBucket(const Slice& key, uint64_t seed) = 0;
};

template <bool cache_aligned>
class OccLockBucketsImpl : public OccLockBucketsImplBase {
 public:
  explicit OccLockBucketsImpl(size_t bucket_count) : locks_(bucket_count) {}
  port::Mutex& GetLockBucket(const Slice& key, uint64_t seed) override {
    return locks_.Get(key, seed);
  }
  size_t ApproximateMemoryUsage() const override {
    return locks_.ApproximateMemoryUsage();
  }

 private:
  // TODO: investigate optionally using folly::MicroLock to majorly save space
  using M = std::conditional_t<cache_aligned, CacheAlignedWrapper<port::Mutex>,
                               port::Mutex>;
  Striped<M> locks_;
};

class OptimisticTransactionDBImpl : public OptimisticTransactionDB {
 public:
  explicit OptimisticTransactionDBImpl(
      DB* db, const OptimisticTransactionDBOptions& occ_options,
      bool take_ownership = true)
      : OptimisticTransactionDB(db),
        db_owner_(take_ownership),
        validate_policy_(occ_options.validate_policy) {
    if (validate_policy_ == OccValidationPolicy::kValidateParallel) {
      auto bucketed_locks = occ_options.shared_lock_buckets;
      if (!bucketed_locks) {
        uint32_t bucket_count = std::max(16u, occ_options.occ_lock_buckets);
        bucketed_locks = MakeSharedOccLockBuckets(bucket_count);
      }
      bucketed_locks_ = static_cast_with_check<OccLockBucketsImplBase>(
          std::move(bucketed_locks));
    }

    // TODO(accheng): init shared data structs here
    num_clusters_ = 256;

    cluster_sched_idx_ = 1;

    std::vector<int> arr {0, /* No-op */
      1,2 //,3,4 ,5,6,7,8,9,10,11,12,13,14,15,16
      // 1,2,3,4,5,6,7,8,//9,10 //,11,12,13,14,15,16,17,18,19,20 //,21,22,23,24,25,26,27,28,29,30,31,32,
    };
    new (&cluster_sched_)(decltype(cluster_sched_))();
    cluster_sched_.resize(arr.size());
    for (uint32_t i = 0; i < cluster_sched_.size(); i++) {
      cluster_sched_[i] = arr[i];
    }

    new (&sched_counts_)(decltype(sched_counts_))();
    sched_counts_.resize(num_clusters_ + 1);
    for (auto& p : sched_counts_) {
      p = std::make_unique<std::atomic<int>>(0);
    }

    num_clients_ = 2;
    new (&app_vt_)(decltype(app_vt_))();
    app_vt_.resize(num_clients_);
    new (&virtual_times_)(decltype(virtual_times_))();
    for (int i = 0; i < num_clients_; ++i) {
      app_vt_[i] = i;
      virtual_times_[i] = i;
    }

    std::vector<std::mutex> temp(num_clusters_ + 1);
    cluster_hash_mutexes_.swap(temp);
    for (int i = 0; i < num_clusters_ + 1; ++i) {
      cluster_hash_[i] = std::vector<WriteCallback *>();
      cluster_hash_fair_[i] = 0;
    }

    for (int j = 0; j < num_clients_; ++j) {
       cluster_hash_clients_[j] = std::map<uint16_t, std::vector<WriteCallback *>>();
      for (int i = 0; i < num_clusters_ + 1; ++i) {
        cluster_hash_clients_[j][i] = std::vector<WriteCallback *>();
      }
    }

    // // TODO(accheng): currently hardcoded
    // max_queue_len_ = 2;

    // new (&sched_txns_)(decltype(sched_txns_))();
    // new (&sched_callbacks_)(decltype(sched_callbacks_))();

    last_index_ = 0;

    // new (&ongoing_txns_)(decltype(ongoing_txns_))();

    // max_clusters_ = 22;
  }

  ~OptimisticTransactionDBImpl() {
    // Prevent this stackable from destroying
    // base db
    if (!db_owner_) {
      db_ = nullptr;
    }
  }

  Transaction* BeginTransaction(const WriteOptions& write_options,
                                const OptimisticTransactionOptions& txn_options,
                                Transaction* old_txn) override;

  // Transactional `DeleteRange()` is not yet supported.
  using StackableDB::DeleteRange;
  virtual Status DeleteRange(const WriteOptions&, ColumnFamilyHandle*,
                             const Slice&, const Slice&) override {
    return Status::NotSupported();
  }

  // Range deletions also must not be snuck into `WriteBatch`es as they are
  // incompatible with `OptimisticTransactionDB`.
  virtual Status Write(const WriteOptions& write_opts,
                       WriteBatch* batch) override {
    if (batch->HasDeleteRange()) {
      return Status::NotSupported();
    }
    return OptimisticTransactionDB::Write(write_opts, batch);
  }

  OccValidationPolicy GetValidatePolicy() const { return validate_policy_; }

  port::Mutex& GetLockBucket(const Slice& key, uint64_t seed) {
    return bucketed_locks_->GetLockBucket(key, seed);
  }

  /** Add trx to appropriate cluster lock queue. */
  void queue_clust_trx(uint16_t cluster, WriteCallback* callback) {
    cluster_hash_mutexes_[cluster].lock();
    cluster_hash_[cluster].push_back(callback);
    cluster_hash_mutexes_[cluster].unlock();
  }

  uint32_t next_sched_idx() {
    uint32_t idx = cluster_sched_idx_ + 1;
    if (idx == cluster_sched_.size()) {
      idx = 1;
    }
    return idx;
  }

  uint32_t prev_sched_idx(uint16_t cluster) {
    uint32_t idx = cluster - 1;
    if (idx == 0) {
      idx = (uint32_t) cluster_sched_.size() - 1;
    }
    return idx;
  }

  /** Check if there are any ongoing transactions. */
  bool check_ongoing_trx() {
    bool ongoing = false;
    for (uint32_t i = 1; i < sched_counts_.size(); ++i) {
      if (sched_counts_[i]->load() != 0) {
        ongoing = true;
        break;
      }
    }

    return ongoing;
  }

  /** Update cluster_sched_idx past first instance of given cluster. */
  void update_sched_idx(uint16_t cluster) {
    cluster_sched_idx_ = cluster + 1;
    if (cluster_sched_idx_ == cluster_sched_.size()) {
      cluster_sched_idx_ = 1;
    }
  }

  Status queue_trx(Transaction* txn) {
    auto txn_impl = reinterpret_cast<Transaction*>(txn);
    txn_impl->SetCV();
    // txn_impl->cv_.wait(lock, [&txn_impl]{ return txn_impl->ready_; });
    return Status::OK();
  }

  bool lock_clust_peek(uint16_t cluster) {
    cluster_hash_mutexes_[cluster].lock();
    bool queued = (cluster_hash_[cluster].size() > 0);
    cluster_hash_mutexes_[cluster].unlock();
    return queued;
  }

  bool lock_clust_peek_fair(uint16_t cluster) {
    cluster_hash_mutexes_[cluster].lock();
    bool queued = (cluster_hash_fair_[cluster] > 0);
    cluster_hash_mutexes_[cluster].unlock();
    return queued;
  }

  bool check_ongoing_phase(uint16_t cluster) {
    if (cluster_sched_idx_ == 0) {
      return true;
    }

    if (sched_counts_[1]->load() == 0 && sched_counts_[2]->load() == 0 &&
        !lock_clust_peek(1) && !lock_clust_peek(2)) {
      return true;
    }

    // trx_sys->cluster_sched_idx represents next cluster
    if (cluster == 22) { // P-W1
      if (cluster_sched_idx_ == 1 && sched_counts_[2]->load() != 0) { // NO-W1
        // std::cout << "NO-GO check_ongoing_phase cluster: " << cluster << " trx_sys->cluster_sched_idx: " << trx_sys->cluster_sched_idx << std::endl;
        return false;
      }
    } else { // P-W2
      if (cluster_sched_idx_ == 2 && sched_counts_[1]->load() != 0) { // NO-W2
        // std::cout << "NO-GO check_ongoing_phase cluster: " << cluster << " trx_sys->cluster_sched_idx: " << trx_sys->cluster_sched_idx << std::endl;
        return false;
      }
    }
    // std::cout << "GO check_ongoing_phase cluster: " << cluster
    // << " trx_sys->cluster_sched_idx: " << trx_sys->cluster_sched_idx
    // << " ongoing count: " << trx_sys->sched_counts[trx_sys->cluster_sched_idx]->load()
    // << std::endl;
    return true;
  }

  bool check_cluster_conflict(uint32_t cluster1, uint32_t cluster2) {
    if (cluster1 == cluster2) {
      return true;
    }

    if ((cluster1 < 11 && cluster2 == 11) || (cluster1 == 11 && cluster2 < 11)) {
      return true;
    }

    if ((cluster1 > 11 && cluster2 == 31) || (cluster1 == 31 && cluster2 > 11)) {
      return true;
    }

    return false;
  }

  // // return (conflict key, whether cluster1 --> cluster2 is a read-write conflict)
  // std::pair<Slice, bool> get_key_conflict(uint32_t cluster1, uint32_t cluster2) {
  //   Slice empty_key = Slice();
  //   std::cout << "get_key_conflict other_cluster: " << cluster1 << " curr_txn_cluster: " << cluster2 << std::endl;

  //   // TODO(accheng): range filter currently hardcoded
  //   if (!check_cluster_conflict(cluster1, cluster2)) {
  //     return std::make_pair(empty_key, false);
  //   }

  //   for (const auto& p1 : hot_key_map_[cluster1]) {
  //     for (const auto& p2 : hot_key_map_[cluster2]) {
  //       std::cout << "key: " << p1.first.ToString() << " vs. key: " << p2.first.ToString() << std::endl;
  //       if (p1.first.compare(p2.first) == 0) { // same key
  //         if (p1.second || p2.second) { // at least one write
  //           bool read_write_conflict = (!p1.second) && p2.second;
  //           return std::make_pair(p1.first, read_write_conflict);
  //         }
  //       }
  //     }
  //   }
  //   return std::make_pair(empty_key, false);
  // }

  // void add_dep(uint32_t parent_idx, uint32_t child_idx, Slice conflict_key) {
  //   std::cout << "add_dep parent_idx: " << parent_idx << " child_idx:" << child_idx << std::endl;

  //   // add parent dep
  //   if (auto s = deps_map_.find(parent_idx); s == deps_map_.end()) { // !deps_map_.contains(parent_idx)
  //     deps_map_[parent_idx] = std::vector<std::pair<uint32_t, Slice>>();
  //   }
  //   deps_map_[parent_idx].emplace_back(std::make_pair(child_idx, conflict_key));

  //   // add child dep
  //   if (auto s = child_deps_map_.find(child_idx); s == child_deps_map_.end()) { // !child_deps_map_.contains(child_idx)
  //     child_deps_map_[child_idx] = std::vector<std::pair<uint32_t, Slice>>();
  //   }
  //   child_deps_map_[child_idx].emplace_back(std::make_pair(parent_idx, conflict_key));
  // }

  // void add_deps(Transaction* txn, uint32_t curr_txn_idx, uint16_t curr_txn_cluster) {
  //   std::cout << "add_deps curr_txn_idx: " << curr_txn_idx << " curr_txn_cluster:" << curr_txn_cluster << std::endl;
  //   bool read_write = false;
  //   bool dep_added = false;
  //   bool deps_made[10] = {false}; // TODO(accheng): currently hardcoded
  //   for (int i = ongoing_txns_.size() - 1; i >= 0; --i) {
  //     uint32_t other_idx = ongoing_txns_[i];
  //     uint16_t other_cluster = txn_map_[other_idx]->GetCluster();
  //     const auto& p = get_key_conflict(other_cluster, curr_txn_cluster);
  //     read_write = p.second;
  //     if (!p.first.empty()) {
  //       size_t bool_idx = other_cluster % 10;
  //       if (read_write && !deps_made[bool_idx]) { // only add one read-write conflict per cluster
  //         dep_added = true;
  //         deps_made[bool_idx] = true;
  //         add_dep(other_idx, curr_txn_idx, p.first);
  //       } else if (read_write) { // all deps for read-write conflict made
  //         bool all_deps = true;
  //         for (bool b : deps_made) {
  //           all_deps &= b;
  //         }
  //         if (all_deps) {
  //           break;
  //         }
  //       } else {
  //         if (!dep_added) { // only add write-* conflict if we haven't added any conflicts before
  //           add_dep(other_idx, curr_txn_idx, p.first);
  //         }
  //         break;
  //       }
  //     }
  //   }

  //   ongoing_txns_.push_back(curr_txn_idx);
  // }

  // void release_sched_txns() {
  //   // build map of transactions sorted by cluster
  //   std::map<uint32_t, std::vector<uint32_t>> sorted_txns_; // cluster, [index from sched_txns_]
  //   for (size_t i = 0; i < sched_txns_.size(); i++) {
  //     uint32_t cluster = sched_txns_[i]->GetCluster();
  //     std::cout << "release_sched_txns cluster: " << cluster << std::endl;
  //     if (auto s = sorted_txns_.find(cluster); s == sorted_txns_.end()) { // !sorted_txns_.contains(cluster)
  //       sorted_txns_[cluster] = std::vector<uint32_t>();
  //     }
  //     sorted_txns_[cluster].push_back(i);
  //   }

  //   // iteratively add deps in sorted order
  //   for(const auto& it : sorted_txns_) {
  //     for (uint32_t idx : it.second) {
  //       last_index_++;
  //       sched_txns_[idx]->SetIndex(last_index_);
  //       txn_map_[last_index_] = sched_txns_[idx];
  //       add_deps(sched_txns_[idx], last_index_, sched_txns_[idx]->GetCluster());
  //     }
  //   }

  //   // release callbacks
  //   sched_txns_.clear();
  //   for (size_t i = 0; i < sched_callbacks_.size(); i++) {
  //     sched_callbacks_[i]->Callback(this);
  //   }
  //   sched_callbacks_.clear();
  // }

  // Status KeyScheduleImpl(uint16_t cluster, const std::vector<Slice>& keys, Transaction* txn, WriteCallback* callback) {
  //   sys_mutex_.lock();
  //   std::cout << "trx cluster: " << cluster << std::endl;
  //   // check how many transactions queued
  //   // if enough, add to dep graph and free all other them and return ok; else, queue up
  //   sched_txns_.push_back(txn);
  //   sched_callbacks_.push_back(callback);

  //   // for (const auto& k : keys) {
  //   //   std::cout << "KeyScheduleImpl key: " << k.ToString() << std::endl;
  //   // }

  //   if (hot_key_map_.size() < max_clusters_) {
  //     if (auto s = hot_key_map_.find(cluster); s == hot_key_map_.end()) { // !hot_key_map_.contains(cluster)
  //       hot_key_map_[cluster] = std::vector<std::pair<Slice, bool>>();
  //       for (size_t i = 0; i < keys.size(); i++) {
  //         // TODO(accheng): hardcoded hot key r/w for now
  //         bool write = false;
  //         if (keys[i].size() > 1) {
  //           if (i == 1) {
  //             write = true;
  //           }
  //         } else {
  //           write = true;
  //         }
  //         hot_key_map_[cluster].emplace_back(std::make_pair(keys[i], write));
  //         // std::cout << "hot_key_map_ key: " << keys[i].ToString() << " write: " << write << std::endl;
  //       }
  //     }
  //   }
  //   std::cout << "hot_key_map_ size: " << hot_key_map_.size() << " keys.size(): " << keys.size() << std::endl;

  //   if (sched_txns_.size() < max_queue_len_) {
  //     sys_mutex_.unlock();

  //     return queue_trx(txn);
  //   } else {
  //     release_sched_txns();

  //     sys_mutex_.unlock();

  //     return Status::OK();
  //   }
  // }

  Status PartialScheduleImpl(uint16_t cluster, Transaction* txn, WriteCallback* callback) {
    sys_mutex_.lock();
    if (lock_clust_peek(cluster) || sched_counts_[cluster]->load() != 0 || !check_ongoing_phase(cluster)) {
      std::cout << "queuing cluster: " << cluster << std::endl;
      queue_clust_trx(cluster, callback);

      sys_mutex_.unlock();

      return queue_trx(txn);
    } else {
      std::cout << "go cluster: " << cluster << std::endl;
      sched_counts_[cluster]->fetch_add(1);

      sys_mutex_.unlock();

      return Status::OK();
    }
  }

  size_t find_p_idx(uint16_t cluster) {
    size_t rem10 = cluster % 10;
    size_t div10 = cluster / 10;
    size_t clust100 = 0;
    if (rem10 == 0) {
      clust100 = div10 - 1;
    } else {
      clust100 = div10;
    }
    return (clust100 + 100 + 1);
  }

  // return whether cluster can execute immediately
  bool check_ongoing_key(uint16_t cluster) {
    if (cluster > 100) {
      int total = 0;
      int idx100 = (cluster % 100); // TODO(accheng): hardcoded
      if (idx100 < 11 && idx100 > 0) {
        for (size_t i = 0; i < 10; i++) { // TPCC
          size_t idx = i + (idx100 - 1) * 10 + 1;
          // std::cout << "idx: " << idx << " idx100: " << idx100 << std::endl;
          total += sched_counts_[idx]->load();
        }
      }
      total += sched_counts_[cluster]->load();
      return (total == 0);
    }

    size_t p_idx = find_p_idx(cluster);
    int total = sched_counts_[cluster]->load();
    total += sched_counts_[p_idx]->load();
    return (total == 0);


    // if (cluster == 21) {
    //   int total = 0;
    //   for (size_t i = 0; i < 10; i++) { // TODO(accheng): hardcoded
    //     total += sched_counts_[i+1]->load();
    //   }
    //   total += sched_counts_[cluster]->load();
    //   return (total == 0);
    // }

    // if (cluster == 22) {
    //   int total = 0;
    //   for (size_t i = 0; i < 10; i++) { // TODO(accheng): hardcoded
    //     total += sched_counts_[i+10+1]->load();
    //   }
    //   total += sched_counts_[cluster]->load();
    //   return (total == 0);
    // }

    // if (cluster < 11) {
    //   int total = sched_counts_[cluster]->load();
    //   total += sched_counts_[21]->load();
    //   return (total == 0);
    // }

    // if (cluster >= 11) {
    //   int total = sched_counts_[cluster]->load();
    //   total += sched_counts_[22]->load();
    //   return (total == 0);
    // }

    // return true;
  }

  void queue_clust_key(uint16_t cluster, WriteCallback* callback) {
    // cluster_hash_mutexes_[cluster].lock();
    cluster_hash_[cluster].push_back(callback);
    // cluster_hash_mutexes_[cluster].unlock();
  }

  void queue_clust_key_fair(uint16_t cluster, uint16_t appId, WriteCallback* callback) {
    // cluster_hash_mutexes_[cluster].lock();
    cluster_hash_fair_[cluster]++;
    cluster_hash_clients_[appId-1][cluster].push_back(callback);
    // cluster_hash_mutexes_[cluster].unlock();
  }

  // void print_size() {
  //   int total = 0;
  //   for (int i = 1; i < 41; i++) {
  //     total += sched_counts_[i]->load();
  //   }
  //   for (int i = 101; i < 105; i++) {
  //     total += sched_counts_[i]->load();
  //   }
  //   std::cout << "total currently running: " << total << std::endl;
  // }

  Status NewScheduleImpl(uint16_t cluster, Transaction* txn, WriteCallback* callback) {
    // std::cout << "trx cluster: " << cluster << std::endl;

    // TODO(accheng): don't queue
    // int key_set_size = 3;
    // if (cluster < 100) {
    //   key_set_size = 5;
    // }
    // double lookup_prob = (2 * 1.0) / key_set_size;
    // double defer_prob = 0.60;
    // int defer = static_cast<int>((lookup_prob * defer_prob) * 100);
    // if ((rand() % 100) >= defer) {
    //   // std::cout << "not queueing cluster: " << cluster << std::endl;
    //   txn->SetCluster(0);
    //   return Status::OK();
    // }

    sys_mutex_.lock();
    // last_index_++;
    // if (last_index_ % 1000 == 0) {
    //   print_size();
    // }

    if (check_ongoing_key(cluster) && !lock_clust_peek(cluster)) {

      sched_counts_[cluster]->fetch_add(1);
      // std::cout << "1-run cluster: " << cluster << std::endl;

      sys_mutex_.unlock();
      return Status::OK();
    } else {

      // std::cout << "2-queueing cluster: " << cluster << std::endl;
      queue_clust_key(cluster, callback);

      sys_mutex_.unlock();
      return queue_trx(txn);
    }
  }

  bool release_clust(uint16_t idx) {
    if (cluster_hash_[idx].size() != 0) {
      sched_counts_[idx]->fetch_add(1);

      cluster_hash_[idx][0]->Callback(this); // TODO(accheng): need to pass db?
      cluster_hash_[idx].erase(cluster_hash_[idx].begin());

      return true;
    }

    return false;
  }

  void key_release_next_clust(uint16_t cluster) {
    if (cluster > 100) {
      int idx100 = cluster % 100;
      // int total = 0;
      for (uint16_t i = 0; i < 10; i++) {
        size_t idx = i + (idx100 - 1) * 10 + 1;
        // total += cluster_hash_[idx].size();
        release_clust(idx);
      }
      // std::cout << "total staring from: " << (idx100 - 1) * 10 + 1 << " is: " << total << std::endl;
    } else {
      size_t p_idx = find_p_idx(cluster);
      // std::cout << "total for: " << p_idx << " is: " << cluster_hash_[p_idx].size() << std::endl;
      if (check_ongoing_key(p_idx)) {
        release_clust(p_idx);
      }
    }

    // bool released = false;
    // if (cluster == 21) {
    //   for (uint16_t i = 0; i < 10; i++) {
    //     uint16_t idx = i + 1;
    //     released |= release_clust(idx);
    //   }

    //   // if (!released) {
    //   //   release_clust(cluster);
    //   // }
    // }

    // if (cluster == 22) {
    //   for (uint16_t i = 0; i < 10; i++) {
    //     uint16_t idx = i + 10 + 1;
    //     released |= release_clust(idx);
    //   }

    //   // if (!released) {
    //   //   release_clust(cluster);
    //   // }
    // }

    // if (cluster < 11) {
    //   if (check_ongoing_key(21)) {
    //     released |= release_clust(21);

    //     // if (!released) {
    //     //   release_clust(cluster);
    //     // }
    //   }
    // }

    // if (cluster >= 11) {
    //   if (check_ongoing_key(22)) {
    //     released |= release_clust(22);

    //     // if (!released) {
    //     //   release_clust(cluster);
    //     // }
    //   }
    // }

    // std::cout << "key_release_next_clust cluster: " << cluster << " released: " << released << std::endl;
  }

  void key_partial_release_next_clust(uint16_t cluster) {
    if (cluster > 100) {
      if (sched_counts_[cluster]->load() == 0) {
        release_clust(cluster);
      }
    } else {
      size_t p_idx = find_p_idx(cluster);
      if (cluster_hash_[p_idx].size() == 0) {
        if (sched_counts_[cluster]->load() == 0) {
          release_clust(cluster);
        }
      } else {
        if (check_ongoing_key(p_idx)) {
          release_clust(p_idx);
        }
      }
    }

    // std::cout << "key_partial_release_next_clust cluster: " << cluster << " queue: " << cluster_hash_[cluster].size() << std::endl;
    // if (cluster == 21) {
    //   if (sched_counts_[cluster]->load() == 0) {
    //     release_clust(cluster);
    //   }
    // }

    // if (cluster == 22) {
    //   if (sched_counts_[cluster]->load() == 0) {
    //     release_clust(cluster);
    //   }
    // }

    // if (cluster < 11) {
    //   // are there other ongoing 1-10 and waiting 21?
    //   // int total = 0;
    //   // for (uint16_t i = 0; i < 10; i++) {
    //   //   uint16_t idx = i + 1;
    //   //   if (idx != cluster) {
    //   //     total += sched_counts_[idx]->load();
    //   //   }
    //   // }

    //   // if (total > 0 &&
    //   if (cluster_hash_[21].size() == 0) {
    //     if (sched_counts_[cluster]->load() == 0) {
    //       release_clust(cluster);
    //     }
    //   } else {
    //     if (check_ongoing_key(21)) {
    //       release_clust(21);
    //     }
    //   }
    // }

    // if (cluster >= 11) {
    //   // int total = 0;
    //   // for (uint16_t i = 0; i < 10; i++) {
    //   //   uint16_t idx = i + 10 + 1;
    //   //   if (idx != cluster) {
    //   //     total += sched_counts_[idx]->load();
    //   //   }
    //   // }

    //   // if (total > 0 &&
    //   if (cluster_hash_[22].size() == 0) {
    //     if (sched_counts_[cluster]->load() == 0) {
    //       release_clust(cluster);
    //     }
    //   } else {
    //     if (check_ongoing_key(22)) {
    //       release_clust(22);
    //     }
    //   }
    // }
  }

  void NewSubCount(uint16_t cluster) {
    sys_mutex_.lock();

    sched_counts_[cluster]->fetch_sub(1);

    // TODO(accheng): release more than 1?

    if (cluster_hash_[cluster].size() == 0) { // sched_counts_[cluster]->load() == 0
      key_release_next_clust(cluster);
    } else {
      key_partial_release_next_clust(cluster);
    }

    sys_mutex_.unlock();
  }

  bool check_turn(uint16_t appId) { // TBU to account for delta
    if (appId != 0) {
      uint16_t appIdIndex = appId - 1;
      if (appIdIndex == virtual_times_.begin()->second){ // check if this app has the lowest vt
        return true;
      } else {
        return false;
      }
    }
    return true;
  }

  Status NewScheduleImplFair(uint16_t cluster, uint16_t appId, Transaction* txn, WriteCallback* callback) {
    sys_mutex_.lock();
    if (check_ongoing_key(cluster) && !lock_clust_peek_fair(cluster)) { // && check_turn(appId)

      sched_counts_[cluster]->fetch_add(1);
      std::cout << "1-run cluster: " << cluster << " appId: " << (appId-1) << std::endl;

      sys_mutex_.unlock();
      return Status::OK();
    } else {

      std::cout << "2-queueing cluster: " << cluster << " appId: " << (appId-1) << std::endl;
      queue_clust_key_fair(cluster, appId, callback);

      sys_mutex_.unlock();
      return queue_trx(txn);
    }
  }

  bool release_clust_fair(uint16_t idx) {
    if (cluster_hash_fair_[idx] != 0) {
      sched_counts_[idx]->fetch_add(1);

      bool released = false;
      // for (const auto& it : virtual_times_) {
      for(auto it = virtual_times_.begin(); it != virtual_times_.end(); ++it) {
        uint16_t appId = it->second;
        std::cout << "iterating release_clust_fair cluster: " << idx << " appId: " << appId
                  << " queued: " << cluster_hash_clients_[appId][idx].size()
                  << " usage: " << app_vt_[appId] << " vt usage: " << it->first << std::endl;
        if (cluster_hash_clients_[appId][idx].size() > 0 && !released) {
          cluster_hash_clients_[appId][idx][0]->Callback(this);
          cluster_hash_clients_[appId][idx].erase(cluster_hash_clients_[appId][idx].begin());
          cluster_hash_fair_[idx]--;
          std::cout << "release_clust_fair cluster: " << idx << " appId: " << appId << std::endl;
          // break;
          released = true;
        }
      }
      return true;
    }
    return false;
  }

  void key_release_next_clust_fair(uint16_t cluster) {
    std::cout << "key_release_next_clust_fair cluster: " << cluster << std::endl;
    if (cluster > 100) {
      int idx100 = cluster % 100;
      // int total = 0;
      for (uint16_t i = 0; i < 10; i++) {
        size_t idx = i + (idx100 - 1) * 10 + 1;
        // total += cluster_hash_[idx].size();
        release_clust_fair(idx);
      }
      // std::cout << "total staring from: " << (idx100 - 1) * 10 + 1 << " is: " << total << std::endl;
    } else {
      size_t p_idx = find_p_idx(cluster);
      // std::cout << "total for: " << p_idx << " is: " << cluster_hash_[p_idx].size() << std::endl;
      if (check_ongoing_key(p_idx)) {
        release_clust_fair(p_idx);
      }
    }
  }

  void key_partial_release_next_clust_fair(uint16_t cluster) {
    std::cout << "key_partial_release_next_clust_fair cluster: " << cluster << std::endl;
    if (cluster > 100) {
      if (sched_counts_[cluster]->load() == 0) {
        release_clust_fair(cluster);
      }
    } else {
      size_t p_idx = find_p_idx(cluster);
      if (cluster_hash_fair_[p_idx] == 0) {
        if (sched_counts_[cluster]->load() == 0) {
          release_clust_fair(cluster);
        }
      } else {
        if (check_ongoing_key(p_idx)) {
          release_clust_fair(p_idx);
        }
      }
    }
  }

  void NewSubCountFair(uint16_t cluster, uint16_t appId, long usage) {
    sys_mutex_.lock();

    uint16_t appIdIndex = appId - 1;
    long old_val = app_vt_[appIdIndex];
    auto it = virtual_times_.find(old_val);
    virtual_times_.erase(it);
    long new_val = old_val + usage;
    while (virtual_times_.find(new_val) != virtual_times_.end()) {
      new_val++;
    }
    app_vt_[appIdIndex] = new_val;
    virtual_times_[new_val] = appIdIndex;
    std::cout << "NewSubCountFair cluster: " << cluster << " appId: " << appIdIndex
              << " usage: " << usage << " new_total: " << new_val << std::endl;

    sched_counts_[cluster]->fetch_sub(1);

    if (cluster_hash_fair_[cluster] == 0) { // sched_counts_[cluster]->load() == 0
      key_release_next_clust_fair(cluster);
    } else {
      key_partial_release_next_clust_fair(cluster);
    }

    sys_mutex_.unlock();
  }

  Status ScheduleImpl(uint16_t cluster, Transaction* txn, WriteCallback* callback) {
    // auto txn_impl = reinterpret_cast<OptimisticTransaction*>(txn);
    // std::cout << "ready_: " << txn_impl->ready_ << std::endl;
  // TODO(accheng): enqueue callback
    std::cout << "trx cluster: " << cluster << std::endl;
    if (cluster_sched_idx_ == 0) {
      sys_mutex_.lock();
      std::cout<< "1-queuing cluster-" << cluster << " count1-" << sched_counts_[prev_sched_idx(cluster)]->load()
            << " count2-" << sched_counts_[cluster]->load() << std::endl;
      if (cluster_sched_idx_ != 0) {
        if (!check_ongoing_trx()) {
          sched_counts_[cluster]->fetch_add(1);

          update_sched_idx(cluster);

          sys_mutex_.unlock();
          return Status::OK();
        }
        queue_clust_trx(cluster, callback);
        sys_mutex_.unlock();
        return queue_trx(txn); // Status::Busy(); // TODO(accheng): update?
      }

      while (cluster_sched_[cluster_sched_idx_] != cluster) {
        cluster_sched_idx_ = next_sched_idx();
      }

      sched_counts_[cluster]->fetch_add(1);

      sys_mutex_.unlock();
      return Status::OK();
    } else {
      sys_mutex_.lock();
      std::cout<< "2-queuing cluster-" << cluster << " count1-" << sched_counts_[prev_sched_idx(cluster)]->load()
          << " count2-" << sched_counts_[cluster]->load() << std::endl;
      if (!check_ongoing_trx()) {
        sched_counts_[cluster]->fetch_add(1);

        update_sched_idx(cluster);

        sys_mutex_.unlock();
        return Status::OK();
      }

      queue_clust_trx(cluster, callback);
      sys_mutex_.unlock();
      return queue_trx(txn); //Status::Busy(); // TODO(accheng): update?
    }
  }

  void check_partial_release(uint16_t cluster) {
    if (cluster == 1 && lock_clust_peek(22) && sched_counts_[22]->load() == 0) { //
      partial_release_next_clust(22);
    } else if (cluster == 2 && lock_clust_peek(21) && sched_counts_[21]->load() == 0) { //
      partial_release_next_clust(21);
    }
  }

  /** Find the next available cluster and release only the next cluster lock
  of that transaction. */
  void partial_release_next_clust(uint16_t cluster) { //
    // uint32_t curr_idx = cluster_sched_idx_;
    // uint16_t cluster = cluster_sched_[curr_idx];

    // NEW CODE
    // if (check_ongoing_phase(cluster)) {

    cluster_hash_mutexes_[cluster].lock();
    std::cout << "release_next_clust cluster: " << cluster << " with num queue: " << cluster_hash_[cluster].size() << std::endl;
    if (cluster_hash_[cluster].size() != 0) {
      // found = true;

      std::cout << "releasing cluster: " << cluster << std::endl;
      sched_counts_[cluster]->fetch_add(1);

      /* Initiate first callback in vector. */
      cluster_hash_[cluster][0]->Callback(this); // TODO(accheng): need to pass db?
      cluster_hash_[cluster].erase(cluster_hash_[cluster].begin());
    }

    cluster_hash_mutexes_[cluster].unlock();

    // NEW CODE
    // }
  }

  /** Find the next available cluster and release the cluster lock of that
  transaction. */
  void release_next_clust() {

    uint32_t curr_idx = cluster_sched_idx_;
    uint16_t cluster = cluster_sched_[curr_idx];

    // bool found = false;
    cluster_hash_mutexes_[cluster].lock();

    std::cout << "release_next_clust cluster_sched_idx:" << cluster_sched_idx_ << " with num queue: " << cluster_hash_[cluster].size() << std::endl;
    while (cluster_hash_[cluster].size() != 0) {
      // found = true;

      std::cout << "releasing cluster: " << cluster << std::endl;
      sched_counts_[cluster]->fetch_add(1);

      /* Initiate first callback in vector. */
      cluster_hash_[cluster][0]->Callback(this); // TODO(accheng): need to pass db?
      cluster_hash_[cluster].erase(cluster_hash_[cluster].begin());
    }

    cluster_sched_idx_ = next_sched_idx();
    cluster_hash_mutexes_[cluster].unlock();

    // NEW CODE
    check_partial_release(cluster);
    // if (!found) {
    //   std::cout << "no locks: " << cluster << std::endl;
    //   print_all_ongoing_trx();
    // }
  }

  void SubCount(uint16_t cluster) {
    sys_mutex_.lock();
    sched_counts_[cluster]->fetch_sub(1);
    // NEW CODE
    // if (cluster > 20) {
      partial_release_next_clust(cluster);
    // } else {
    // // OLD CODE
    // if (sched_counts_[cluster]->load() == 0) {
    //   release_next_clust();
    // }
    // }

    sys_mutex_.unlock();
  }

  // // check if this txn is dependent on any ongoing txns
  // bool CheckConflict(const Slice& key, Transaction* txn) {
  //   sys_mutex_.lock();

  //   bool conflict = false;
  //   uint32_t txn_idx = txn->GetIndex();

  //   if (auto s = child_deps_map_.find(txn_idx); s != child_deps_map_.end()) {
  //     for (auto& p : child_deps_map_[txn_idx]) {
  //       if (key.compare(p.second) == 0) {
  //         conflict = true;
  //         break;
  //       }
  //     }
  //   }

  //   sys_mutex_.unlock();

  //   return conflict;
  // }

  // void ScheduleKeyImpl(const Slice& key, Transaction* txn, WriteCallback* callback) {
  //   sys_mutex_.lock(); // TODO(accheng): is this too expensive?

  //   uint32_t txn_idx = txn->GetIndex();
  //   bool queued = false;
  //   std::cout << "ScheduleKeyImpl key: " << key.ToString() << " txn_idx: " << txn_idx << std::endl;

  //   if (auto s = child_deps_map_.find(txn_idx); s != child_deps_map_.end()) { // child_deps_map_.contains(txn_idx)
  //     for (auto& p : child_deps_map_[txn_idx]) {
  //       if (key.compare(p.second) == 0) {
  //          std::cout << "queueing key: " << key.ToString() << std::endl;
  //         callback_map_[txn_idx] = callback;
  //         sys_mutex_.unlock();

  //         queue_trx(txn);

  //         queued = true;
  //         break;
  //       }
  //     }
  //   }

  //   if (!queued) {
  //     sys_mutex_.unlock();
  //   }
  // }

  // void release_deps(uint32_t txn_idx) {
  //   if (auto s = deps_map_.find(txn_idx); s == deps_map_.end()) { // no deps to free !deps_map_.contains(txn_idx)
  //     return;
  //   }

  //   for (size_t i = 0; i < deps_map_[txn_idx].size(); i++) {
  //     uint32_t child_idx = deps_map_[txn_idx][i].first;
  //     const auto& it = std::find(child_deps_map_[child_idx].begin(), child_deps_map_[child_idx].end(),
  //                                std::make_pair(txn_idx, deps_map_[txn_idx][i].second));
  //     if (it != child_deps_map_[child_idx].end()) {
  //       child_deps_map_[child_idx].erase(it);
  //     }

  //     // free child txn if no more deps
  //     if (child_deps_map_[child_idx].empty()) {
  //       child_deps_map_.erase(child_idx);

  //       if (auto vp = callback_map_.find(child_idx); vp != callback_map_.end()) {
  //         callback_map_[child_idx]->Callback(this);
  //         callback_map_.erase(child_idx);
  //       }
  //     }
  //   }
  //   deps_map_.erase(txn_idx);


  //   ongoing_txns_.erase(std::remove(ongoing_txns_.begin(), ongoing_txns_.end(), txn_idx),
  //                       ongoing_txns_.end());
  //   txn_map_.erase(txn_idx);
  // }

  // void SubDepCount(Transaction* txn) {
  //   sys_mutex_.lock();

  //   release_deps(txn->GetIndex());

  //   sys_mutex_.unlock();
  // }

 private:
  std::shared_ptr<OccLockBucketsImplBase> bucketed_locks_;

  bool db_owner_;

  const OccValidationPolicy validate_policy_;

  // TODO(accheng): shared data structs here
  std::mutex sys_mutex_;

  uint16_t num_clusters_;

  uint16_t cluster_sched_idx_;
  std::vector<uint16_t> cluster_sched_;
  std::vector<std::unique_ptr<std::atomic_int>> sched_counts_;

  uint16_t num_clients_;
  std::vector<long> app_vt_; // <appId, vt>
  std::map<long, uint16_t> virtual_times_; // <vt, appId>

  // Protected map of <cluster, callbacks>
  std::vector<std::mutex> cluster_hash_mutexes_;
  std::map<uint16_t, std::vector<WriteCallback *>> cluster_hash_;

  std::map<uint16_t, uint16_t> cluster_hash_fair_; // <cluster, number of queued txns>
  std::map<uint16_t, std::map<uint16_t, std::vector<WriteCallback *>>> cluster_hash_clients_;

  // // protected by sys_mutex
  // uint32_t max_queue_len_; // threshold on batch size
  // std::vector<Transaction *> sched_txns_; // txns to be scheduled
  // std::vector<WriteCallback *> sched_callbacks_; // corresponding callbacks

  uint32_t last_index_;
  // std::map<uint32_t, Transaction *> txn_map_; // <index, txn>
  // std::map<uint32_t, WriteCallback *> callback_map_; // <index, callback>
  // std::vector<uint32_t> ongoing_txns_; // txns ordered by index
  // std::map<uint32_t, std::vector<std::pair<uint32_t, Slice>>> deps_map_; // <parent index, [(child index, conflicting key)]
  // std::map<uint32_t, std::vector<std::pair<uint32_t, Slice>>> child_deps_map_; // <child index, [(parent index, conflicting key)]>

  // uint32_t max_clusters_;
  // std::map<uint32_t, std::vector<std::pair<Slice, bool>>> hot_key_map_; // <cluster, [(possible hot key, boolean for r/w)]>

  void ReinitializeTransaction(Transaction* txn,
                               const WriteOptions& write_options,
                               const OptimisticTransactionOptions& txn_options =
                                   OptimisticTransactionOptions());
};

}  // namespace ROCKSDB_NAMESPACE
