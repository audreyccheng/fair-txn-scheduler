//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <iostream>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "db/db_iter.h"
#include "db/read_callback.h"
#include "db/snapshot_checker.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/utilities/transaction_db.h"
#include "util/cast_util.h"
#include "utilities/transactions/lock/lock_manager.h"
#include "utilities/transactions/lock/range/range_lock_manager.h"
#include "utilities/transactions/pessimistic_transaction.h"
#include "utilities/transactions/write_prepared_txn.h"

namespace ROCKSDB_NAMESPACE {

class PessimisticTransactionDB : public TransactionDB {
 public:
  explicit PessimisticTransactionDB(DB* db,
                                    const TransactionDBOptions& txn_db_options);

  explicit PessimisticTransactionDB(StackableDB* db,
                                    const TransactionDBOptions& txn_db_options);

  virtual ~PessimisticTransactionDB();

  virtual const Snapshot* GetSnapshot() override { return db_->GetSnapshot(); }

  virtual Status Initialize(
      const std::vector<size_t>& compaction_enabled_cf_indices,
      const std::vector<ColumnFamilyHandle*>& handles);

  Transaction* BeginTransaction(const WriteOptions& write_options,
                                const TransactionOptions& txn_options,
                                Transaction* old_txn) override = 0;

  using StackableDB::Put;
  virtual Status Put(const WriteOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     const Slice& val) override;

  using StackableDB::Delete;
  virtual Status Delete(const WriteOptions& wopts,
                        ColumnFamilyHandle* column_family,
                        const Slice& key) override;

  using StackableDB::SingleDelete;
  virtual Status SingleDelete(const WriteOptions& wopts,
                              ColumnFamilyHandle* column_family,
                              const Slice& key) override;

  using StackableDB::Merge;
  virtual Status Merge(const WriteOptions& options,
                       ColumnFamilyHandle* column_family, const Slice& key,
                       const Slice& value) override;

  using TransactionDB::Write;
  virtual Status Write(const WriteOptions& opts, WriteBatch* updates) override;
  inline Status WriteWithConcurrencyControl(const WriteOptions& opts,
                                            WriteBatch* updates) {
    Status s;
    if (opts.protection_bytes_per_key > 0) {
      s = WriteBatchInternal::UpdateProtectionInfo(
          updates, opts.protection_bytes_per_key);
    }
    if (s.ok()) {
      // Need to lock all keys in this batch to prevent write conflicts with
      // concurrent transactions.
      Transaction* txn = BeginInternalTransaction(opts);
      txn->DisableIndexing();

      auto txn_impl = static_cast_with_check<PessimisticTransaction>(txn);

      // Since commitBatch sorts the keys before locking, concurrent Write()
      // operations will not cause a deadlock.
      // In order to avoid a deadlock with a concurrent Transaction,
      // Transactions should use a lock timeout.
      s = txn_impl->CommitBatch(updates);

      delete txn;
    }

    return s;
  }

  using StackableDB::CreateColumnFamily;
  virtual Status CreateColumnFamily(const ColumnFamilyOptions& options,
                                    const std::string& column_family_name,
                                    ColumnFamilyHandle** handle) override;

  Status CreateColumnFamilies(
      const ColumnFamilyOptions& options,
      const std::vector<std::string>& column_family_names,
      std::vector<ColumnFamilyHandle*>* handles) override;

  Status CreateColumnFamilies(
      const std::vector<ColumnFamilyDescriptor>& column_families,
      std::vector<ColumnFamilyHandle*>* handles) override;

  using StackableDB::DropColumnFamily;
  virtual Status DropColumnFamily(ColumnFamilyHandle* column_family) override;

  Status DropColumnFamilies(
      const std::vector<ColumnFamilyHandle*>& column_families) override;

  Status TryLock(PessimisticTransaction* txn, uint32_t cfh_id,
                 const std::string& key, bool exclusive);
  Status TryRangeLock(PessimisticTransaction* txn, uint32_t cfh_id,
                      const Endpoint& start_endp, const Endpoint& end_endp);

  void UnLock(PessimisticTransaction* txn, const LockTracker& keys);
  void UnLock(PessimisticTransaction* txn, uint32_t cfh_id,
              const std::string& key);

  void AddColumnFamily(const ColumnFamilyHandle* handle);

  static TransactionDBOptions ValidateTxnDBOptions(
      const TransactionDBOptions& txn_db_options);

  const TransactionDBOptions& GetTxnDBOptions() const {
    return txn_db_options_;
  }

  void InsertExpirableTransaction(TransactionID tx_id,
                                  PessimisticTransaction* tx);
  void RemoveExpirableTransaction(TransactionID tx_id);

  // If transaction is no longer available, locks can be stolen
  // If transaction is available, try stealing locks directly from transaction
  // It is the caller's responsibility to ensure that the referred transaction
  // is expirable (GetExpirationTime() > 0) and that it is expired.
  bool TryStealingExpiredTransactionLocks(TransactionID tx_id);

  Transaction* GetTransactionByName(const TransactionName& name) override;

  void RegisterTransaction(Transaction* txn);
  void UnregisterTransaction(Transaction* txn);

  // not thread safe. current use case is during recovery (single thread)
  void GetAllPreparedTransactions(std::vector<Transaction*>* trans) override;

  LockManager::PointLockStatus GetLockStatusData() override;

  std::vector<DeadlockPath> GetDeadlockInfoBuffer() override;
  void SetDeadlockInfoBufferSize(uint32_t target_size) override;

  // The default implementation does nothing. The actual implementation is moved
  // to the child classes that actually need this information. This was due to
  // an odd performance drop we observed when the added std::atomic member to
  // the base class even when the subclass do not read it in the fast path.
  virtual void UpdateCFComparatorMap(const std::vector<ColumnFamilyHandle*>&) {}
  virtual void UpdateCFComparatorMap(ColumnFamilyHandle*) {}

  // Use the returned factory to create LockTrackers in transactions.
  const LockTrackerFactory& GetLockTrackerFactory() const {
    return lock_manager_->GetLockTrackerFactory();
  }

  std::pair<Status, std::shared_ptr<const Snapshot>> CreateTimestampedSnapshot(
      TxnTimestamp ts) override;

  std::shared_ptr<const Snapshot> GetTimestampedSnapshot(
      TxnTimestamp ts) const override;

  void ReleaseTimestampedSnapshotsOlderThan(TxnTimestamp ts) override;

  Status GetTimestampedSnapshots(TxnTimestamp ts_lb, TxnTimestamp ts_ub,
                                 std::vector<std::shared_ptr<const Snapshot>>&
                                     timestamped_snapshots) const override;

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
  }

  void queue_clust_key(uint16_t cluster, Transaction* txn) {
    // cluster_hash_mutexes_[cluster].lock();
    cluster_hash_[cluster].push_back(txn);
    // cluster_hash_mutexes_[cluster].unlock();
  }

  Status NewScheduleImpl(uint16_t cluster, Transaction* txn) {
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
      queue_clust_key(cluster, txn);

      sys_mutex_.unlock();
      return queue_trx(txn);
    }
  }

  bool release_clust(uint16_t idx) {
    if (cluster_hash_[idx].size() != 0) {
      // std::cout << "releasing cluster: " << idx << std::endl;
      sched_counts_[idx]->fetch_add(1);

      cluster_hash_[idx][0]->ReleaseCV();
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
  }

  void NewSubCount(uint16_t cluster) {
    sys_mutex_.lock();

    sched_counts_[cluster]->fetch_sub(1);
    // std::cout << "NewSubCount cluster: " << cluster << " sched_count: " << sched_counts_[cluster]->load() <<  std::endl;

    // TODO(accheng): release more than 1?

    if (cluster_hash_[cluster].size() == 0) { // sched_counts_[cluster]->load() == 0
      key_release_next_clust(cluster);
    } else {
      key_partial_release_next_clust(cluster);
    }

    sys_mutex_.unlock();
  }

 protected:
  DBImpl* db_impl_;
  std::shared_ptr<Logger> info_log_;
  const TransactionDBOptions txn_db_options_;

  static Status FailIfBatchHasTs(const WriteBatch* wb);

  static Status FailIfCfEnablesTs(const DB* db,
                                  const ColumnFamilyHandle* column_family);

  void ReinitializeTransaction(
      Transaction* txn, const WriteOptions& write_options,
      const TransactionOptions& txn_options = TransactionOptions());

  virtual Status VerifyCFOptions(const ColumnFamilyOptions& cf_options);

 private:
  friend class WritePreparedTxnDB;
  friend class WritePreparedTxnDBMock;
  friend class WriteUnpreparedTxn;
  friend class TransactionTest_DoubleCrashInRecovery_Test;
  friend class TransactionTest_DoubleEmptyWrite_Test;
  friend class TransactionTest_DuplicateKeys_Test;
  friend class TransactionTest_PersistentTwoPhaseTransactionTest_Test;
  friend class TransactionTest_TwoPhaseDoubleRecoveryTest_Test;
  friend class TransactionTest_TwoPhaseOutOfOrderDelete_Test;
  friend class TransactionStressTest_TwoPhaseLongPrepareTest_Test;
  friend class WriteUnpreparedTransactionTest_RecoveryTest_Test;
  friend class WriteUnpreparedTransactionTest_MarkLogWithPrepSection_Test;

  Transaction* BeginInternalTransaction(const WriteOptions& options);

  std::shared_ptr<LockManager> lock_manager_;

  // Must be held when adding/dropping column families.
  InstrumentedMutex column_family_mutex_;

  // Used to ensure that no locks are stolen from an expirable transaction
  // that has started a commit. Only transactions with an expiration time
  // should be in this map.
  std::mutex map_mutex_;
  std::unordered_map<TransactionID, PessimisticTransaction*>
      expirable_transactions_map_;

  // map from name to two phase transaction instance
  std::mutex name_map_mutex_;
  std::unordered_map<TransactionName, Transaction*> transactions_;

  // TODO(accheng): shared data structs here
  std::mutex sys_mutex_;

  uint16_t num_clusters_;

  uint16_t cluster_sched_idx_;
  std::vector<uint16_t> cluster_sched_;
  std::vector<std::unique_ptr<std::atomic_int>> sched_counts_;

  // Protected map of <cluster, callbacks>
  std::vector<std::mutex> cluster_hash_mutexes_;
  std::map<uint16_t, std::vector<Transaction *>> cluster_hash_;

  // Signal that we are testing a crash scenario. Some asserts could be relaxed
  // in such cases.
  virtual void TEST_Crash() {}
};

// A PessimisticTransactionDB that writes the data to the DB after the commit.
// In this way the DB only contains the committed data.
class WriteCommittedTxnDB : public PessimisticTransactionDB {
 public:
  explicit WriteCommittedTxnDB(DB* db,
                               const TransactionDBOptions& txn_db_options)
      : PessimisticTransactionDB(db, txn_db_options) {}

  explicit WriteCommittedTxnDB(StackableDB* db,
                               const TransactionDBOptions& txn_db_options)
      : PessimisticTransactionDB(db, txn_db_options) {}

  virtual ~WriteCommittedTxnDB() {}

  Transaction* BeginTransaction(const WriteOptions& write_options,
                                const TransactionOptions& txn_options,
                                Transaction* old_txn) override;

  // Optimized version of ::Write that makes use of skip_concurrency_control
  // hint
  using TransactionDB::Write;
  virtual Status Write(const WriteOptions& opts,
                       const TransactionDBWriteOptimizations& optimizations,
                       WriteBatch* updates) override;
  virtual Status Write(const WriteOptions& opts, WriteBatch* updates) override;
};

inline Status PessimisticTransactionDB::FailIfBatchHasTs(
    const WriteBatch* batch) {
  if (batch != nullptr && WriteBatchInternal::HasKeyWithTimestamp(*batch)) {
    return Status::NotSupported(
        "Writes with timestamp must go through transaction API instead of "
        "TransactionDB.");
  }
  return Status::OK();
}

inline Status PessimisticTransactionDB::FailIfCfEnablesTs(
    const DB* db, const ColumnFamilyHandle* column_family) {
  assert(db);
  column_family = column_family ? column_family : db->DefaultColumnFamily();
  assert(column_family);
  const Comparator* const ucmp = column_family->GetComparator();
  assert(ucmp);
  if (ucmp->timestamp_size() > 0) {
    return Status::NotSupported(
        "Write operation with user timestamp must go through the transaction "
        "API instead of TransactionDB.");
  }
  return Status::OK();
}

class SnapshotCreationCallback : public PostMemTableCallback {
 public:
  explicit SnapshotCreationCallback(
      DBImpl* dbi, TxnTimestamp commit_ts,
      const std::shared_ptr<TransactionNotifier>& notifier,
      std::shared_ptr<const Snapshot>& snapshot)
      : db_impl_(dbi),
        commit_ts_(commit_ts),
        snapshot_notifier_(notifier),
        snapshot_(snapshot) {
    assert(db_impl_);
  }

  ~SnapshotCreationCallback() override {
    snapshot_creation_status_.PermitUncheckedError();
  }

  Status operator()(SequenceNumber seq, bool disable_memtable) override;

 private:
  DBImpl* const db_impl_;
  const TxnTimestamp commit_ts_;
  std::shared_ptr<TransactionNotifier> snapshot_notifier_;
  std::shared_ptr<const Snapshot>& snapshot_;

  Status snapshot_creation_status_;
};

}  // namespace ROCKSDB_NAMESPACE
