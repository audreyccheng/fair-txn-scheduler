jl/db/obsolete_files_test.cc.d db/obsolete_files_test.o \
  jl/db/obsolete_files_test.o: db/obsolete_files_test.cc \
  db/db_impl/db_impl.h db/column_family.h \
  cache/cache_reservation_manager.h cache/cache_entry_roles.h \
  include/rocksdb/cache.h include/rocksdb/compression_type.h \
  include/rocksdb/rocksdb_namespace.h include/rocksdb/data_structure.h \
  include/rocksdb/memory_allocator.h include/rocksdb/customizable.h \
  include/rocksdb/configurable.h include/rocksdb/status.h \
  include/rocksdb/slice.h include/rocksdb/cleanable.h cache/cache_key.h \
  table/unique_id_impl.h include/rocksdb/unique_id.h \
  include/rocksdb/table_properties.h include/rocksdb/types.h \
  cache/typed_cache.h cache/cache_helpers.h \
  include/rocksdb/advanced_cache.h include/rocksdb/advanced_options.h \
  include/rocksdb/memtablerep.h include/rocksdb/universal_compaction.h \
  util/coding.h port/port.h port/port_posix.h \
  include/rocksdb/port_defs.h util/coding_lean.h db/memtable_list.h \
  db/logs_with_prep_tracker.h db/memtable.h db/dbformat.h \
  include/rocksdb/comparator.h include/rocksdb/slice_transform.h \
  util/user_comparator_wrapper.h monitoring/perf_context_imp.h \
  monitoring/perf_step_timer.h monitoring/perf_level_imp.h \
  include/rocksdb/perf_level.h monitoring/statistics_impl.h \
  monitoring/histogram.h include/rocksdb/statistics.h port/likely.h \
  util/core_local.h util/random.h util/mutexlock.h util/fastrange.h \
  util/hash.h include/rocksdb/system_clock.h \
  include/rocksdb/perf_context.h util/stop_watch.h db/kv_checksum.h \
  db/range_tombstone_fragmenter.h db/pinned_iterators_manager.h \
  table/internal_iterator.h file/readahead_file_info.h \
  include/rocksdb/iterator.h include/rocksdb/wide_columns.h \
  table/format.h file/file_prefetch_buffer.h include/rocksdb/env.h \
  include/rocksdb/functor_wrapper.h include/rocksdb/thread_status.h \
  include/rocksdb/file_system.h include/rocksdb/io_status.h \
  include/rocksdb/options.h include/rocksdb/file_checksum.h \
  include/rocksdb/listener.h include/rocksdb/compaction_job_stats.h \
  include/rocksdb/sst_partitioner.h include/rocksdb/version.h \
  include/rocksdb/write_buffer_manager.h include/rocksdb/table.h \
  util/aligned_buffer.h util/autovector.h port/lang.h \
  file/random_access_file_reader.h env/file_system_tracer.h \
  trace_replay/io_tracer.h monitoring/instrumented_mutex.h \
  include/rocksdb/trace_record.h trace_replay/trace_replay.h \
  include/rocksdb/utilities/replayer.h include/rocksdb/rate_limiter.h \
  memory/memory_allocator_impl.h options/cf_options.h \
  options/db_options.h util/compression.h table/block_based/block_type.h \
  test_util/sync_point.h util/compression_context_cache.h \
  util/string_util.h port/malloc.h db/read_callback.h db/version_edit.h \
  db/blob/blob_file_addition.h db/blob/blob_constants.h \
  db/blob/blob_file_garbage.h db/wal_edit.h logging/event_logger.h \
  logging/log_buffer.h memory/arena.h memory/allocator.h port/mmap.h \
  port/sys_time.h table/table_reader.h \
  include/rocksdb/table_reader_caller.h table/get_context.h \
  table/multiget_context.h db/lookup_key.h db/merge_context.h \
  util/async_file_reader.h util/math.h util/single_thread_executor.h \
  memory/concurrent_arena.h util/thread_local.h include/rocksdb/db.h \
  include/rocksdb/block_cache_trace_writer.h \
  include/rocksdb/trace_reader_writer.h include/rocksdb/metadata.h \
  include/rocksdb/snapshot.h include/rocksdb/sst_file_writer.h \
  include/rocksdb/transaction_log.h include/rocksdb/write_batch.h \
  include/rocksdb/write_batch_base.h util/dynamic_bloom.h \
  util/hash_containers.h db/range_del_aggregator.h \
  db/compaction/compaction_iteration_stats.h \
  table/scoped_arena_iterator.h table/table_builder.h \
  db/seqno_to_time_mapping.h db/table_properties_collector.h \
  file/writable_file_writer.h trace_replay/block_cache_tracer.h \
  util/heap.h util/kv_map.h file/filename.h db/table_cache.h \
  util/coro_utils.h db/write_batch_internal.h db/flush_scheduler.h \
  db/trim_history_scheduler.h db/write_thread.h \
  db/post_memtable_callback.h db/pre_release_callback.h \
  db/write_callback.h util/cast_util.h db/write_controller.h \
  db/compaction/compaction_iterator.h db/compaction/compaction.h \
  db/version_set.h db/blob/blob_file_meta.h db/blob/blob_index.h \
  db/compaction/compaction_picker.h db/file_indexer.h db/log_reader.h \
  db/log_format.h file/sequence_file_reader.h util/udt_util.h \
  util/xxhash.h db/version_builder.h db/merge_helper.h \
  db/snapshot_checker.h include/rocksdb/compaction_filter.h \
  include/rocksdb/merge_operator.h db/compaction/compaction_job.h \
  db/blob/blob_file_completion_callback.h db/error_handler.h \
  db/event_helpers.h file/sst_file_manager_impl.h \
  file/delete_scheduler.h include/rocksdb/sst_file_manager.h \
  db/compaction/compaction_outputs.h db/blob/blob_garbage_meter.h \
  db/internal_stats.h db/output_validator.h db/job_context.h \
  db/log_writer.h db/external_sst_file_ingestion_job.h \
  db/snapshot_impl.h db/flush_job.h db/import_column_family_job.h \
  db/periodic_task_scheduler.h util/timer.h db/wal_manager.h \
  file/file_util.h table/merging_iterator.h table/iterator_wrapper.h \
  util/repeatable_thread.h db/db_test_util.h \
  include/rocksdb/convenience.h include/rocksdb/filter_policy.h \
  include/rocksdb/utilities/checkpoint.h table/mock_table.h \
  test_util/testharness.h port/stack_trace.h test_util/testutil.h \
  env/composite_env_wrapper.h utilities/merge_operators.h
