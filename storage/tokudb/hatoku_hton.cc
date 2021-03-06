/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "$Id$"
/*======
This file is part of TokuDB


Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    TokuDBis is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2,
    as published by the Free Software Foundation.

    TokuDB is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TokuDB.  If not, see <http://www.gnu.org/licenses/>.

======= */

#ident \
    "Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved."

#include "hatoku_hton.h"
#include "src/ydb.h"

#include <dlfcn.h>

#include "my_tree.h"

#define TOKU_METADB_NAME "tokudb_meta"

#if defined(HAVE_PSI_MUTEX_INTERFACE)
static pfs_key_t tokudb_map_mutex_key;

static PSI_mutex_info all_tokudb_mutexes[] = {
    {&tokudb_map_mutex_key, "tokudb_map_mutex", 0},
    {&ha_tokudb_mutex_key, "ha_tokudb_mutex", 0},
};

static PSI_rwlock_info all_tokudb_rwlocks[] = {
    {&num_DBs_lock_key, "num_DBs_lock", 0},
};
#endif /* HAVE_PSI_MUTEX_INTERFACE */

typedef struct savepoint_info {
  DB_TXN *txn;
  tokudb_trx_data *trx;
  bool in_sub_stmt;
} * SP_INFO, SP_INFO_T;

static SERVICE_TYPE(registry) *reg_srv = nullptr;
SERVICE_TYPE(log_builtins) *log_bi = nullptr;
SERVICE_TYPE(log_builtins_string) *log_bs = nullptr;

static handler *tokudb_create_handler(handlerton *hton, TABLE_SHARE *table,
                                      bool partitioned, MEM_ROOT *mem_root);
/** Return partitioning flags. */
static uint tokudb_partition_flags();

static void tokudb_print_error(const DB_ENV *db_env, const char *db_errpfx,
                               const char *buffer);
static void tokudb_cleanup_log_files(void);
static int tokudb_end(handlerton *hton, ha_panic_function type);
static bool tokudb_flush_logs(handlerton *hton, bool binlog_group_commit);
static bool tokudb_show_status(handlerton *hton, THD *thd, stat_print_fn *print,
                               enum ha_stat_type);
static int tokudb_close_connection(handlerton *hton, THD *thd);
static void tokudb_kill_connection(handlerton *hton, THD *thd);
static int tokudb_commit(handlerton *hton, THD *thd, bool all);
static int tokudb_rollback(handlerton *hton, THD *thd, bool all);
static int tokudb_xa_prepare(handlerton *hton, THD *thd, bool all);
static int tokudb_xa_recover(handlerton *hton, XA_recover_txn *txn_list,
                             uint len, MEM_ROOT *mem_root);

static xa_status_code tokudb_commit_by_xid(handlerton *hton, XID *xid);
static xa_status_code tokudb_rollback_by_xid(handlerton *hton, XID *xid);
static int tokudb_rollback_to_savepoint(handlerton *hton, THD *thd,
                                        void *savepoint);
static int tokudb_savepoint(handlerton *hton, THD *thd, void *savepoint);
static int tokudb_release_savepoint(handlerton *hton, THD *thd,
                                    void *savepoint);
#if defined(TOKU_INCLUDE_DISCOVER_FRM) && TOKU_INCLUDE_DISCOVER_FRM
static int tokudb_discover(handlerton *hton, THD *thd, const char *db,
                           const char *name, uchar **frmblob, size_t *frmlen);
static int tokudb_discover2(handlerton *hton, THD *thd, const char *db,
                            const char *name, bool translate_name,
                            uchar **frmblob, size_t *frmlen);
static int tokudb_discover3(handlerton *hton, THD *thd, const char *db,
                            const char *name, char *path, uchar **frmblob,
                            size_t *frmlen);
#endif  // defined(TOKU_INCLUDE_DISCOVER_FRM) && TOKU_INCLUDE_DISCOVER_FRM
handlerton *tokudb_hton;

const char *ha_tokudb_ext = ".tokudb";
DB_ENV *db_env;

#if defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
static tokudb::thread::mutex_t tokudb_map_mutex;
static TREE tokudb_map;
struct tokudb_map_pair {
  THD *thd;
  char *last_lock_timeout;
};
static int tokudb_map_pair_cmp(TOKUDB_UNUSED(const void *custom_arg),
                               const void *a, const void *b) {
  const struct tokudb_map_pair *a_key = (const struct tokudb_map_pair *)a;
  const struct tokudb_map_pair *b_key = (const struct tokudb_map_pair *)b;
  if (a_key->thd < b_key->thd)
    return -1;
  else if (a_key->thd > b_key->thd)
    return +1;
  else
    return 0;
};
#endif  // defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG

static PARTITIONED_COUNTER tokudb_primary_key_bytes_inserted;
void toku_hton_update_primary_key_bytes_inserted(uint64_t row_size) {
  increment_partitioned_counter(tokudb_primary_key_bytes_inserted, row_size);
}

static void tokudb_lock_timeout_callback(DB *db, uint64_t requesting_txnid,
                                         const DBT *left_key,
                                         const DBT *right_key,
                                         uint64_t blocking_txnid);

#define ASSERT_MSGLEN 1024

void toku_hton_assert_fail(const char *expr_as_string, const char *fun,
                           const char *file, int line, int caller_errno) {
  char msg[ASSERT_MSGLEN];
  if (db_env) {
    snprintf(msg, ASSERT_MSGLEN, "Handlerton: %s ", expr_as_string);
    db_env->crash(db_env, msg, fun, file, line, caller_errno);
  } else {
    snprintf(msg, ASSERT_MSGLEN,
             "Handlerton assertion failed, no env, %s, %d, %s, %s (errno=%d)\n",
             file, line, fun, expr_as_string, caller_errno);
    perror(msg);
    fflush(stderr);
  }
  abort();
}

// bool tokudb_shared_data = false;
static uint32_t tokudb_init_flags = DB_CREATE | DB_THREAD | DB_PRIVATE |
                                    DB_INIT_LOCK | DB_INIT_MPOOL | DB_INIT_TXN |
                                    DB_INIT_LOG | DB_RECOVER;
static uint32_t tokudb_env_flags = 0;
// static uint32_t tokudb_lock_type = DB_LOCK_DEFAULT;
// static ulong tokudb_log_buffer_size = 0;
// static ulong tokudb_log_file_size = 0;
static char *tokudb_home;
// static long tokudb_lock_scan_time = 0;
// static ulong tokudb_region_size = 0;
// static ulong tokudb_cache_parts = 1;
const char *tokudb_hton_name = "TokuDB";

// All TokuDB and PerconaFT exts that might appear in a database dir
static const char *ha_tokudb_exts[]{".tokudb", nullptr};

#if defined(_WIN32)
extern "C" {
#include "ydb.h"
}
#endif

// A flag set if the handlerton is in an initialized, usable state,
// plus a reader-write lock to protect it without serializing reads.
// Since we don't have static initializers for the opaque rwlock type,
// use constructor and destructor functions to create and destroy
// the lock before and after main(), respectively.
int tokudb_hton_initialized;

// tokudb_hton_initialized_lock can not be instrumented as it must be
// initialized before mysql_mutex_register() call to protect
// some globals from race condition.
tokudb::thread::rwlock_t tokudb_hton_initialized_lock;

static SHOW_VAR *toku_global_status_variables = NULL;
static uint64_t toku_global_status_max_rows;
static TOKU_ENGINE_STATUS_ROW_S *toku_global_status_rows = NULL;

static void handle_ydb_error(int error) {
  switch (error) {
    case TOKUDB_HUGE_PAGES_ENABLED:
      LogPluginErrMsg(ERROR_LEVEL, 0,
                      "Can not run with transparent huge pages enabled. "
                      "Please disable them to continue. (echo never > "
                      "/sys/kernel/mm/transparent_hugepage/enabled)");
      break;
    case TOKUDB_UPGRADE_FAILURE:
      LogPluginErrMsg(
          ERROR_LEVEL, 0,
          "Upgrade failed. A clean shutdown of the previous version is "
          "required.");
      break;
    default:
      LogPluginErrMsg(ERROR_LEVEL, 0, "Unknown error %d", error);
      break;
  }
}

static int tokudb_set_product_name(void) {
  size_t n = strlen(tokudb_hton_name);
  char tokudb_product_name[n + 1];
  memset(tokudb_product_name, 0, sizeof tokudb_product_name);
  for (size_t i = 0; i < n; i++)
    tokudb_product_name[i] = tolower(tokudb_hton_name[i]);
  int r = db_env_set_toku_product_name(tokudb_product_name);
  return r;
}

extern "C" {
extern uint force_recovery;
}

static int tokudb_init_func(void *p) {
  TOKUDB_DBUG_ENTER("%p", p);
  int r;

  int mode = force_recovery
                 ? S_IRUSR | S_IRGRP | S_IROTH
                 : S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  if (force_recovery != 0 && (!read_only || !super_read_only)) {
    LogPluginErrMsg(ERROR_LEVEL, 0,
                    "Not initialized because tokudb_force_only requires "
                    "read_only and super_read_only");
    goto error;
  }

  // 3938: lock the handlerton's initialized status flag for writing
  rwlock_t_lock_write(tokudb_hton_initialized_lock);

  // Initialize error logging service.
  if (init_logging_service_for_plugin(&reg_srv, &log_bi, &log_bs)) {
    tokudb_hton_initialized_lock.unlock();
    DBUG_RETURN(true);
  }

#if defined(HAVE_PSI_INTERFACE)
  /* Register TokuDB mutex keys with MySQL performance schema */
  int count;

  count = array_elements(all_tokudb_mutexes);
  mysql_mutex_register("tokudb", all_tokudb_mutexes, count);

  count = array_elements(all_tokudb_rwlocks);
  mysql_rwlock_register("tokudb", all_tokudb_rwlocks, count);

  tokudb_map_mutex.reinit(tokudb_map_mutex_key);
#endif /* HAVE_PSI_INTERFACE */

  db_env = NULL;
  tokudb_hton = (handlerton *)p;

  if (tokudb::sysvars::check_jemalloc) {
    typedef int (*mallctl_type)(const char *, void *, size_t *, void *, size_t);
    mallctl_type mallctl_func;
    mallctl_func = (mallctl_type)dlsym(RTLD_DEFAULT, "mallctl");
    if (!mallctl_func) {
      LogPluginErrMsg(ERROR_LEVEL, 0,
                      "Not initialized because jemalloc is not loaded");
      goto error;
    }
    char *ver;
    size_t len = sizeof(ver);
    mallctl_func("version", &ver, &len, NULL, 0);
    /* jemalloc 2.2.5 crashes mysql-test */
    if (strcmp(ver, "2.3.") < 0) {
      LogPluginErrMsg(ERROR_LEVEL, 0,
                      "Not initialized because jemalloc is older than 2.3.0");
      goto error;
    }
  }

  r = tokudb_set_product_name();
  if (r) {
    LogPluginErrMsg(ERROR_LEVEL, 0, "Can not set product name error %d", r);
    goto error;
  }

  TOKUDB_SHARE::static_init();
  tokudb::background::initialize();

  tokudb_hton->state = SHOW_OPTION_YES;
  // tokudb_hton->flags= HTON_CAN_RECREATE;  // QQQ this came from skeleton
  tokudb_hton->flags = HTON_CLOSE_CURSORS_AT_COMMIT;

#if defined(TOKU_INCLUDE_EXTENDED_KEYS) && TOKU_INCLUDE_EXTENDED_KEYS
#if defined(HTON_SUPPORTS_EXTENDED_KEYS)
  tokudb_hton->flags |= HTON_SUPPORTS_EXTENDED_KEYS;
#endif
#if defined(HTON_EXTENDED_KEYS)
  tokudb_hton->flags |= HTON_EXTENDED_KEYS;
#endif
#endif
#if defined(HTON_SUPPORTS_CLUSTERED_KEYS)
  tokudb_hton->flags |= HTON_SUPPORTS_CLUSTERED_KEYS;
#endif

#if defined(TOKU_USE_DB_TYPE_TOKUDB) && TOKU_USE_DB_TYPE_TOKUDB
  tokudb_hton->db_type = DB_TYPE_TOKUDB;
#elif defined(TOKU_USE_DB_TYPE_UNKNOWN) && TOKU_USE_DB_TYPE_UNKNOWN
  tokudb_hton->db_type = DB_TYPE_UNKNOWN;
#else
#error
#endif

  tokudb_hton->create = tokudb_create_handler;
  tokudb_hton->partition_flags = tokudb_partition_flags;
  tokudb_hton->close_connection = tokudb_close_connection;
  tokudb_hton->kill_connection = tokudb_kill_connection;

  tokudb_hton->savepoint_offset = sizeof(SP_INFO_T);
  tokudb_hton->savepoint_set = tokudb_savepoint;
  tokudb_hton->savepoint_rollback = tokudb_rollback_to_savepoint;
  tokudb_hton->savepoint_release = tokudb_release_savepoint;

#if defined(TOKU_INCLUDE_DISCOVER_FRM) && TOKU_INCLUDE_DISCOVER_FRM
  tokudb_hton->discover = tokudb_discover;
#if defined(MYSQL_HANDLERTON_INCLUDE_DISCOVER2)
  tokudb_hton->discover2 = tokudb_discover2;
#endif  // MYSQL_HANDLERTON_INCLUDE_DISCOVER2
#endif  // defined(TOKU_INCLUDE_DISCOVER_FRM) && TOKU_INCLUDE_DISCOVER_FRM
  tokudb_hton->commit = tokudb_commit;
  tokudb_hton->rollback = tokudb_rollback;
  tokudb_hton->prepare = tokudb_xa_prepare;
  tokudb_hton->recover = tokudb_xa_recover;
  tokudb_hton->commit_by_xid = tokudb_commit_by_xid;
  tokudb_hton->rollback_by_xid = tokudb_rollback_by_xid;

  tokudb_hton->panic = tokudb_end;
  tokudb_hton->flush_logs = tokudb_flush_logs;
  tokudb_hton->show_status = tokudb_show_status;
  tokudb_hton->file_extensions = ha_tokudb_exts;

  if (!tokudb_home) tokudb_home = mysql_real_data_home;
  DBUG_PRINT("info", ("tokudb_home: %s", tokudb_home));

  if ((r = db_env_create(&db_env, 0))) {
    DBUG_PRINT("info", ("db_env_create %d\n", r));
    handle_ydb_error(r);
    goto error;
  }

  DBUG_PRINT("info", ("tokudb_env_flags: 0x%x\n", tokudb_env_flags));
  r = db_env->set_flags(db_env, tokudb_env_flags, 1);
  if (r) {  // QQQ
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_INIT, "WARNING: flags=%x r=%d",
                           tokudb_env_flags, r);
    // goto error;
  }

  // config error handling
  db_env->set_errcall(db_env, tokudb_print_error);
  db_env->set_errpfx(db_env, tokudb_hton_name);

  //
  // set default comparison functions
  //
  r = db_env->set_default_bt_compare(db_env, tokudb_cmp_dbt_key);
  if (r) {
    DBUG_PRINT("info", ("set_default_bt_compare%d\n", r));
    goto error;
  }

  {
    char *tmp_dir = tokudb::sysvars::tmp_dir;
    char *data_dir = tokudb::sysvars::data_dir;
    if (data_dir == 0) {
      data_dir = mysql_data_home;
    }
    if (tmp_dir == 0) {
      tmp_dir = data_dir;
    }
    DBUG_PRINT("info", ("tokudb_data_dir: %s\n", data_dir));
    db_env->set_data_dir(db_env, data_dir);
    DBUG_PRINT("info", ("tokudb_tmp_dir: %s\n", tmp_dir));
    db_env->set_tmp_dir(db_env, tmp_dir);
  }

  if (tokudb::sysvars::log_dir) {
    DBUG_PRINT("info", ("tokudb_log_dir: %s\n", tokudb::sysvars::log_dir));
    db_env->set_lg_dir(db_env, tokudb::sysvars::log_dir);
  }

  // config the cache table size to min(1/2 of physical memory, 1/8 of the
  // process address space)
  if (tokudb::sysvars::cache_size == 0) {
    uint64_t physmem, maxdata;
    physmem = toku_os_get_phys_memory_size();
    tokudb::sysvars::cache_size = physmem / 2;
    r = toku_os_get_max_process_data_size(&maxdata);
    if (r == 0) {
      if (tokudb::sysvars::cache_size > maxdata / 8)
        tokudb::sysvars::cache_size = maxdata / 8;
    }
  }
  if (tokudb::sysvars::cache_size) {
    DBUG_PRINT("info",
               ("tokudb_cache_size: %lld\n", tokudb::sysvars::cache_size));
    r = db_env->set_cachesize(
        db_env, (uint32_t)(tokudb::sysvars::cache_size >> 30),
        (uint32_t)(tokudb::sysvars::cache_size % (1024L * 1024L * 1024L)), 1);
    if (r) {
      DBUG_PRINT("info", ("set_cachesize %d\n", r));
      goto error;
    }
  }
  if (tokudb::sysvars::max_lock_memory == 0) {
    tokudb::sysvars::max_lock_memory = tokudb::sysvars::cache_size / 8;
  } else if (tokudb::sysvars::max_lock_memory < HA_TOKUDB_MIN_LOCK_MEMORY) {
    LogPluginErrMsg(WARNING_LEVEL, 0,
                    "tokudb_max_lock_memory must be greater than %u.  The "
                    "current set value of %llu is too small and is being set "
                    "to %u.",
                    HA_TOKUDB_MIN_LOCK_MEMORY, tokudb::sysvars::max_lock_memory,
                    HA_TOKUDB_MIN_LOCK_MEMORY);
    tokudb::sysvars::max_lock_memory = HA_TOKUDB_MIN_LOCK_MEMORY;
  }
  if (tokudb::sysvars::max_lock_memory) {
    DBUG_PRINT("info", ("tokudb_max_lock_memory: %lld\n",
                        tokudb::sysvars::max_lock_memory));
    r = db_env->set_lk_max_memory(db_env, tokudb::sysvars::max_lock_memory);
    if (r) {
      DBUG_PRINT("info", ("set_lk_max_memory %d\n", r));
      goto error;
    }
  }

  uint32_t gbytes, bytes;
  int parts;
  r = db_env->get_cachesize(db_env, &gbytes, &bytes, &parts);
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_INIT, "tokudb_cache_size=%lld r=%d",
                         ((unsigned long long)gbytes << 30) + bytes, r);

  r = db_env->set_client_pool_threads(db_env,
                                      tokudb::sysvars::client_pool_threads);
  if (r) {
    DBUG_PRINT("info", ("set_client_pool_threads %d\n", r));
    goto error;
  }

  r = db_env->set_cachetable_pool_threads(
      db_env, tokudb::sysvars::cachetable_pool_threads);
  if (r) {
    DBUG_PRINT("info", ("set_cachetable_pool_threads %d\n", r));
    goto error;
  }

  r = db_env->set_checkpoint_pool_threads(
      db_env, tokudb::sysvars::checkpoint_pool_threads);
  if (r) {
    DBUG_PRINT("info", ("set_checkpoint_pool_threads %d\n", r));
    goto error;
  }

  if (db_env->set_redzone) {
    r = db_env->set_redzone(db_env, tokudb::sysvars::fs_reserve_percent);
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_INIT, "set_redzone r=%d", r);
  }
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_INIT, "env open:flags=%x",
                         tokudb_init_flags);

  r = db_env->set_generate_row_callback_for_put(db_env, generate_row_for_put);
  assert_always(r == 0);

  r = db_env->set_generate_row_callback_for_del(db_env, generate_row_for_del);
  assert_always(r == 0);

  db_env->set_update(db_env, tokudb_update_fun);

  db_env_set_direct_io(tokudb::sysvars::directio);

  db_env_set_compress_buffers_before_eviction(
      tokudb::sysvars::compress_buffers_before_eviction);

  db_env->change_fsync_log_period(db_env, tokudb::sysvars::fsync_log_period);

  db_env->set_lock_timeout_callback(db_env, tokudb_lock_timeout_callback);
  db_env->set_dir_per_db(db_env, tokudb::sysvars::dir_per_db);

  db_env->set_loader_memory_size(db_env,
                                 tokudb_get_loader_memory_size_callback);

  db_env->set_check_thp(db_env, tokudb::sysvars::check_jemalloc);

  r = db_env->open(db_env, tokudb_home, tokudb_init_flags, mode);

  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_INIT, "env opened:return=%d", r);

  if (r) {
    DBUG_PRINT("info", ("env->open %d", r));
    handle_ydb_error(r);
    goto error;
  }

  r = db_env->checkpointing_set_period(db_env,
                                       tokudb::sysvars::checkpointing_period);
  assert_always(r == 0);

  r = db_env->cleaner_set_period(db_env, tokudb::sysvars::cleaner_period);
  assert_always(r == 0);

  r = db_env->cleaner_set_iterations(db_env,
                                     tokudb::sysvars::cleaner_iterations);
  assert_always(r == 0);

  r = db_env->set_lock_timeout(db_env, DEFAULT_TOKUDB_LOCK_TIMEOUT,
                               tokudb_get_lock_wait_time_callback);
  assert_always(r == 0);

  r = db_env->evictor_set_enable_partial_eviction(
      db_env, tokudb::sysvars::enable_partial_eviction);
  assert_always(r == 0);

  db_env->set_killed_callback(db_env, DEFAULT_TOKUDB_KILLED_TIME,
                              tokudb_get_killed_time_callback,
                              tokudb_killed_callback);

  r = db_env->get_engine_status_num_rows(db_env, &toku_global_status_max_rows);
  assert_always(r == 0);

  {
    const myf mem_flags =
        MY_FAE | MY_WME | MY_ZEROFILL | MY_ALLOW_ZERO_PTR | MY_FREE_ON_ERROR;
    toku_global_status_variables = (SHOW_VAR *)tokudb::memory::malloc(
        sizeof(*toku_global_status_variables) * toku_global_status_max_rows,
        mem_flags);
    toku_global_status_rows =
        (TOKU_ENGINE_STATUS_ROW_S *)tokudb::memory::malloc(
            sizeof(*toku_global_status_rows) * toku_global_status_max_rows,
            mem_flags);
  }

  tokudb_primary_key_bytes_inserted = create_partitioned_counter();

#if defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
  init_tree(&tokudb_map, 0, 0, tokudb_map_pair_cmp, true, NULL, NULL);
#endif  // defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG

  if (tokudb::sysvars::strip_frm_data) {
    r = tokudb::metadata::strip_frm_data(db_env);
    if (r) {
      DBUG_PRINT("info", ("env->open %d", r));
      handle_ydb_error(r);
      goto error;
    }
  }

  // 3938: succeeded, set the init status flag and unlock
  tokudb_hton_initialized = 1;
  tokudb_hton_initialized_lock.unlock();
  DBUG_RETURN(false);

error:
  if (db_env) {
    int rr = db_env->close(db_env, 0);
    assert_always(rr == 0);
    db_env = 0;
  }

  deinit_logging_service_for_plugin(&reg_srv, &log_bi, &log_bs);

  // 3938: failed to initialized, drop the flag and lock
  tokudb_hton_initialized = 0;
  tokudb_hton_initialized_lock.unlock();
  DBUG_RETURN(true);
}

static int tokudb_done_func(TOKUDB_UNUSED(void *p)) {
  TOKUDB_DBUG_ENTER("");
  tokudb::memory::free(toku_global_status_variables);
  toku_global_status_variables = NULL;
  tokudb::memory::free(toku_global_status_rows);
  toku_global_status_rows = NULL;
  tokudb_map_mutex.deinit();
  toku_ydb_destroy();
  deinit_logging_service_for_plugin(&reg_srv, &log_bi, &log_bs);
  TOKUDB_DBUG_RETURN(0);
}

static handler *tokudb_create_handler(handlerton *hton, TABLE_SHARE *table,
                                      bool partitioned, MEM_ROOT *mem_root) {
  if (partitioned) {
    assert(partitioned);
    ha_tokupart *file = new (mem_root) ha_tokupart(hton, table);
    if (file && file->init_partitioning(mem_root)) {
      destroy(file);
      return (nullptr);
    }
    return (file);
  }

  return new (mem_root) ha_tokudb(hton, table);
}

static uint tokudb_partition_flags() { return HA_CANNOT_PARTITION_FK; }

int tokudb_end(TOKUDB_UNUSED(handlerton *hton),
               TOKUDB_UNUSED(ha_panic_function type)) {
  TOKUDB_DBUG_ENTER("");
  int error = 0;

  // 3938: if we finalize the storage engine plugin, it is no longer
  // initialized. grab a writer lock for the duration of the
  // call, so we can drop the flag and destroy the mutexes
  // in isolation.
  rwlock_t_lock_write(tokudb_hton_initialized_lock);
  assert_always(tokudb_hton_initialized);

  tokudb::background::destroy();
  TOKUDB_SHARE::static_destroy();

  if (db_env) {
    if (tokudb_init_flags & DB_INIT_LOG) tokudb_cleanup_log_files();

    // count the total number of prepared txn's that we discard
    long total_prepared = 0;
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "begin XA cleanup");
    while (1) {
      // get xid's
      const long n_xid = 1;
      TOKU_XA_XID xids[n_xid];
      long n_prepared = 0;
      error = db_env->txn_xa_recover(db_env, xids, n_xid, &n_prepared,
                                     total_prepared == 0 ? DB_FIRST : DB_NEXT);
      assert_always(error == 0);
      if (n_prepared == 0) break;
      // discard xid's
      for (long i = 0; i < n_xid; i++) {
        DB_TXN *txn = NULL;
        error = db_env->get_txn_from_xid(db_env, &xids[i], &txn);
        assert_always(error == 0);
        error = txn->discard(txn, 0);
        assert_always(error == 0);
      }
      total_prepared += n_prepared;
    }
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "end XA cleanup");
    error =
        db_env->close(db_env, total_prepared > 0 ? TOKUFT_DIRTY_SHUTDOWN : 0);
    if (error != 0 && total_prepared > 0) {
      LogPluginErrMsg(ERROR_LEVEL, 0,
                      "%ld prepared txns still live, please shutdown, error %d",
                      total_prepared, error);
    } else
      assert_always(error == 0);
    db_env = NULL;
  }

  if (tokudb_primary_key_bytes_inserted) {
    destroy_partitioned_counter(tokudb_primary_key_bytes_inserted);
    tokudb_primary_key_bytes_inserted = NULL;
  }

#if defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
  delete_tree(&tokudb_map);
#endif  // defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG

  // 3938: drop the initialized flag and unlock
  tokudb_hton_initialized = 0;
  tokudb_hton_initialized_lock.unlock();

  TOKUDB_DBUG_RETURN(error);
}

static int tokudb_close_connection(TOKUDB_UNUSED(handlerton *hton), THD *thd) {
  int error = 0;
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, tokudb_hton);
  if (trx && trx->checkpoint_lock_taken) {
    error = db_env->checkpointing_resume(db_env);
  }
  tokudb::memory::free(trx);
#if defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
  mutex_t_lock(tokudb_map_mutex);
  struct tokudb_map_pair key = {thd, NULL};
  struct tokudb_map_pair *found_key =
      (struct tokudb_map_pair *)tree_search(&tokudb_map, &key, NULL);

  if (found_key) {
    tokudb::memory::free(found_key->last_lock_timeout);
    tree_delete(&tokudb_map, found_key, sizeof(*found_key), NULL);
  }
  mutex_t_unlock(tokudb_map_mutex);
#endif  // defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
  return error;
}

void tokudb_kill_connection(TOKUDB_UNUSED(handlerton *hton), THD *thd) {
  TOKUDB_DBUG_ENTER("");
  db_env->kill_waiter(db_env, thd);
  DBUG_VOID_RETURN;
}

bool tokudb_flush_logs(TOKUDB_UNUSED(handlerton *hton),
                       bool binlog_group_commit) {
  TOKUDB_DBUG_ENTER("");
  int error;
  bool result = 0;

  // if we are in 'FLUSH LOGS' and we are directed to checkpoint, do a
  // checkpoint which also has the effect of flushing logs
  if (!binlog_group_commit && tokudb::sysvars::checkpoint_on_flush_logs) {
    error = db_env->txn_checkpoint(db_env, 0, 0, 0);
    if (error) {
      my_error(ER_ERROR_DURING_FLUSH_LOGS, MYF(0), error);
      result = 1;
      goto exit;
    }
  }
  // if we are either in 'FLUSH LOGS', or, we are not in 'FLUSH LOGS' but in
  // binlog_group_commit and we are in high durability, flush 'em
  else if (!binlog_group_commit || (tokudb::sysvars::fsync_log_period == 0 &&
                                    tokudb::sysvars::commit_sync(NULL))) {
    error = db_env->log_flush(db_env, NULL);
    assert_always(error == 0);
  }

exit:
  TOKUDB_DBUG_RETURN(result);
}

typedef struct txn_progress_info {
  char status[200];
  THD *thd;
} * TXN_PROGRESS_INFO;

static void txn_progress_func(TOKU_TXN_PROGRESS progress, void *extra) {
  TXN_PROGRESS_INFO progress_info = (TXN_PROGRESS_INFO)extra;
  int r = sprintf(progress_info->status,
                  "%sprocessing %s of transaction, %" PRId64 " out of %" PRId64,
                  progress->stalled_on_checkpoint
                      ? "Writing committed changes to disk, "
                      : "",
                  progress->is_commit ? "commit" : "abort",
                  progress->entries_processed, progress->entries_total);
  assert_always(r >= 0);
  thd_proc_info(progress_info->thd, progress_info->status);
}

static void commit_txn_with_progress(DB_TXN *txn, uint32_t flags, THD *thd) {
  const char *orig_proc_info = tokudb_thd_get_proc_info(thd);
  struct txn_progress_info info;
  info.thd = thd;
  int r = txn->commit_with_progress(txn, flags, txn_progress_func, &info);
  if (r != 0) {
    LogPluginErrMsg(ERROR_LEVEL, 0,
                    "Tried committing transaction %p and got error code %d",
                    txn, r);
  }
  assert_always(r == 0);
  thd_proc_info(thd, orig_proc_info);
}

static void abort_txn_with_progress(DB_TXN *txn, THD *thd) {
  const char *orig_proc_info = tokudb_thd_get_proc_info(thd);
  struct txn_progress_info info;
  info.thd = thd;
  int r = txn->abort_with_progress(txn, txn_progress_func, &info);
  if (r != 0) {
    LogPluginErrMsg(ERROR_LEVEL, 0,
                    "Tried aborting transaction %p and got error code %d", txn,
                    r);
  }
  assert_always(r == 0);
  thd_proc_info(thd, orig_proc_info);
}

static void tokudb_cleanup_handlers(tokudb_trx_data *trx, DB_TXN *txn) {
  LIST *e;
  while ((e = trx->handlers)) {
    trx->handlers = list_delete(trx->handlers, e);
    ha_tokudb *handler = (ha_tokudb *)e->data;
    handler->cleanup_txn(txn);
  }
}

// Determine if an fsync is used when a transaction is committed.
static bool tokudb_sync_on_commit(THD *thd) {
  // Check the client durability property which is set during 2PC
  if (thd_get_durability_property(thd) == HA_IGNORE_DURABILITY) return false;
  if (tokudb::sysvars::fsync_log_period > 0) return false;
  return tokudb::sysvars::commit_sync(thd) != 0;
}

static int tokudb_commit(handlerton *hton, THD *thd, bool all) {
  TOKUDB_DBUG_ENTER("%u", all);
  DBUG_PRINT("trans", ("ending transaction %s", all ? "all" : "stmt"));
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, hton);
  DB_TXN **txn = all ? &trx->all : &trx->stmt;
  DB_TXN *this_txn = *txn;
  if (this_txn) {
    uint32_t syncflag = tokudb_sync_on_commit(thd) ? 0 : DB_TXN_NOSYNC;
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "commit trx %u txn %p syncflag %u",
                           all, this_txn, syncflag);
    // test hook to induce a crash on a debug build
    DBUG_EXECUTE_IF("tokudb_crash_commit_before", DBUG_SUICIDE(););
    tokudb_cleanup_handlers(trx, this_txn);
    commit_txn_with_progress(this_txn, syncflag, thd);
    // test hook to induce a crash on a debug build
    DBUG_EXECUTE_IF("tokudb_crash_commit_after", DBUG_SUICIDE(););
    *txn = NULL;
    trx->sub_sp_level = NULL;
    if (this_txn == trx->sp_level || trx->all == NULL) {
      trx->sp_level = NULL;
    }
  } else {
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "nothing to commit %d", all);
  }
  reset_stmt_progress(&trx->stmt_progress);
  TOKUDB_DBUG_RETURN(0);
}

static int tokudb_rollback(handlerton *hton, THD *thd, bool all) {
  TOKUDB_DBUG_ENTER("%u", all);
  DBUG_PRINT("trans", ("aborting transaction %s", all ? "all" : "stmt"));
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, hton);
  DB_TXN **txn = all ? &trx->all : &trx->stmt;
  DB_TXN *this_txn = *txn;
  if (this_txn) {
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "rollback %u txn %p", all,
                           this_txn);
    tokudb_cleanup_handlers(trx, this_txn);
    abort_txn_with_progress(this_txn, thd);
    *txn = NULL;
    trx->sub_sp_level = NULL;
    if (this_txn == trx->sp_level || trx->all == NULL) {
      trx->sp_level = NULL;
    }
  } else {
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "abort0");
  }
  reset_stmt_progress(&trx->stmt_progress);
  TOKUDB_DBUG_RETURN(0);
}

static bool tokudb_sync_on_prepare(THD *thd) {
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "enter");
  bool r;
  // skip sync of log if fsync log period > 0 or if
  // client durability during 2PC has been set to ignore, usually because
  // binlog coordinator is in use and performing group commit
  if (tokudb::sysvars::fsync_log_period > 0 ||
      thd_get_durability_property(thd) == HA_IGNORE_DURABILITY) {
    r = false;
  } else {
    r = true;
  }
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "exit %d", r);
  return r;
}

static int tokudb_xa_prepare(handlerton *hton, THD *thd, bool all) {
  TOKUDB_DBUG_ENTER("%u", all);
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "enter");
  int r = 0;

  DBUG_PRINT("trans", ("preparing transaction %s", all ? "all" : "stmt"));
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, hton);
  DB_TXN *txn = all ? trx->all : trx->stmt;
  if (txn) {
    uint32_t syncflag = tokudb_sync_on_prepare(thd) ? 0 : DB_TXN_NOSYNC;
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "doing txn prepare:%d:%p", all,
                           txn);
    // a TOKU_XA_XID is identical to a MYSQL_XID
    TOKU_XA_XID thd_xid;
    thd_get_xid(thd, (MYSQL_XID *)&thd_xid);
    // test hook to induce a crash on a debug build
    DBUG_EXECUTE_IF("tokudb_crash_prepare_before", DBUG_SUICIDE(););
    r = txn->xa_prepare(txn, &thd_xid, syncflag);
    // test hook to induce a crash on a debug build
    DBUG_EXECUTE_IF("tokudb_crash_prepare_after", DBUG_SUICIDE(););

    // XA log entries can be interleaved in the binlog since XA prepare on
    // the master flushes to the binlog.  There can be log entries from
    // different clients pushed into the binlog before XA commit is executed
    // on the master.  Therefore, the slave thread must be able to juggle
    // multiple XA transactions.  Tokudb does this by zapping the client
    // transaction context on the slave when executing the XA prepare and
    // expecting to process XA commit with commit_by_xid (which supplies the
    // XID so that the transaction can be looked up and committed).
    if (r == 0 && all && thd->slave_thread) {
      TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "zap txn context %u",
                             thd_sql_command(thd));
      if (thd_sql_command(thd) == SQLCOM_XA_PREPARE) {
        trx->all = NULL;
        trx->sub_sp_level = NULL;
        trx->sp_level = NULL;
      }
    }
  } else {
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "nothing to prepare %d", all);
  }
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "exit %d", r);
  TOKUDB_DBUG_RETURN(r);
}

static int tokudb_xa_recover(TOKUDB_UNUSED(handlerton *hton),
                             XA_recover_txn *txn_list, uint len,
                             TOKUDB_UNUSED(MEM_ROOT *mem_root)) {
  TOKUDB_DBUG_ENTER("");
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "enter");
  int r = 0;
  if (len == 0 || txn_list == NULL) {
    TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "exit %d", 0);
    TOKUDB_DBUG_RETURN(0);
  }
  std::vector<TOKU_XA_XID> xids;
  xids.resize(len);

  long num_returned = 0;
  r = db_env->txn_xa_recover(db_env, &xids[0], len, &num_returned, DB_NEXT);

  uint count = 0;
  for (; count < num_returned; count++) {
    const auto &trans = xids[count];
    txn_list[count].id.set(trans.formatID, trans.data, trans.gtrid_length,
                           trans.data + trans.gtrid_length, trans.bqual_length);

    txn_list[count].mod_tables = new (mem_root) List<st_handler_tablename>();
    if (!txn_list[count].mod_tables) break;
  }
  assert_always(r == 0);
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "exit %d", count);
  TOKUDB_DBUG_RETURN((int)count);
}

static xa_status_code tokudb_commit_by_xid(TOKUDB_UNUSED(handlerton *hton),
                                           XID *xid) {
  TOKUDB_DBUG_ENTER("");
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "enter");
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "xid %p", xid);
  int r = 0;
  DB_TXN *txn = NULL;
  TOKU_XA_XID *toku_xid = (TOKU_XA_XID *)xid;

  r = db_env->get_txn_from_xid(db_env, toku_xid, &txn);
  if (r) {
    goto cleanup;
  }

  r = txn->commit(txn, 0);
  if (r) {
    goto cleanup;
  }

  r = 0;
cleanup:
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "exit %d", r);
  if (TOKUDB_UNLIKELY(
          (tokudb::sysvars::debug & TOKUDB_DEBUG_RETURN) ||
          (r != 0 && (tokudb::sysvars::debug & TOKUDB_DEBUG_ERROR)))) {
    TOKUDB_TRACE("return %d", r);
  }
  DBUG_RETURN(r == 0 ? XA_OK : XAER_RMERR);
}

static xa_status_code tokudb_rollback_by_xid(TOKUDB_UNUSED(handlerton *hton),
                                             XID *xid) {
  TOKUDB_DBUG_ENTER("");
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "enter");
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "xid %p", xid);
  int r = 0;
  DB_TXN *txn = NULL;
  TOKU_XA_XID *toku_xid = (TOKU_XA_XID *)xid;

  r = db_env->get_txn_from_xid(db_env, toku_xid, &txn);
  if (r) {
    goto cleanup;
  }

  r = txn->abort(txn);
  if (r) {
    goto cleanup;
  }

  r = 0;
cleanup:
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_XA, "exit %d", r);
  if (TOKUDB_UNLIKELY(
          (tokudb::sysvars::debug & TOKUDB_DEBUG_RETURN) ||
          (r != 0 && (tokudb::sysvars::debug & TOKUDB_DEBUG_ERROR)))) {
    TOKUDB_TRACE("return %d", r);
  }
  DBUG_RETURN(r == 0 ? XA_OK : XAER_RMERR);
}

static int tokudb_savepoint(handlerton *hton, THD *thd, void *savepoint) {
  TOKUDB_DBUG_ENTER("%p", savepoint);
  int error;
  SP_INFO save_info = (SP_INFO)savepoint;
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, hton);
  if (thd->in_sub_stmt) {
    assert_always(trx->stmt);
    error = txn_begin(db_env, trx->sub_sp_level, &(save_info->txn),
                      DB_INHERIT_ISOLATION, thd);
    if (error) {
      goto cleanup;
    }
    trx->sub_sp_level = save_info->txn;
    save_info->in_sub_stmt = true;
  } else {
    error = txn_begin(db_env, trx->sp_level, &(save_info->txn),
                      DB_INHERIT_ISOLATION, thd);
    if (error) {
      goto cleanup;
    }
    trx->sp_level = save_info->txn;
    save_info->in_sub_stmt = false;
  }
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "begin txn %p", save_info->txn);
  save_info->trx = trx;
  error = 0;
cleanup:
  TOKUDB_DBUG_RETURN(error);
}

static int tokudb_rollback_to_savepoint(handlerton *hton, THD *thd,
                                        void *savepoint) {
  TOKUDB_DBUG_ENTER("%p", savepoint);
  int error;
  SP_INFO save_info = (SP_INFO)savepoint;
  DB_TXN *parent = NULL;
  DB_TXN *txn_to_rollback = save_info->txn;

  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, hton);
  parent = txn_to_rollback->parent;
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "rollback txn %p", txn_to_rollback);
  if (!(error = txn_to_rollback->abort(txn_to_rollback))) {
    if (save_info->in_sub_stmt) {
      trx->sub_sp_level = parent;
    } else {
      trx->sp_level = parent;
    }
    error = tokudb_savepoint(hton, thd, savepoint);
  }
  TOKUDB_DBUG_RETURN(error);
}

static int tokudb_release_savepoint(handlerton *hton, THD *thd,
                                    void *savepoint) {
  TOKUDB_DBUG_ENTER("%p", savepoint);
  int error = 0;
  SP_INFO save_info = (SP_INFO)savepoint;
  DB_TXN *parent = NULL;
  DB_TXN *txn_to_commit = save_info->txn;

  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, hton);
  parent = txn_to_commit->parent;
  TOKUDB_TRACE_FOR_FLAGS(TOKUDB_DEBUG_TXN, "commit txn %p", txn_to_commit);
  DB_TXN *child = txn_to_commit->get_child(txn_to_commit);
  if (child == NULL && !(error = txn_to_commit->commit(txn_to_commit, 0))) {
    if (save_info->in_sub_stmt) {
      trx->sub_sp_level = parent;
    } else {
      trx->sp_level = parent;
    }
  }
  save_info->txn = NULL;
  TOKUDB_DBUG_RETURN(error);
}

#if defined(TOKU_INCLUDE_DISCOVER_FRM) && TOKU_INCLUDE_DISCOVER_FRM
static int tokudb_discover(handlerton *hton, THD *thd, const char *db,
                           const char *name, uchar **frmblob, size_t *frmlen) {
  return tokudb_discover2(hton, thd, db, name, true, frmblob, frmlen);
}

static int tokudb_discover2(handlerton *hton, THD *thd, const char *db,
                            const char *name, bool translate_name,
                            uchar **frmblob, size_t *frmlen) {
  char path[FN_REFLEN + 1];
  build_table_filename(path, sizeof(path) - 1, db, name, "",
                       translate_name ? 0 : FN_IS_TMP);
  return tokudb_discover3(hton, thd, db, name, path, frmblob, frmlen);
}

static int tokudb_discover3(TOKUDB_UNUSED(handlerton *hton), THD *thd,
                            const char *db, const char *name, char *path,
                            uchar **frmblob, size_t *frmlen) {
  TOKUDB_DBUG_ENTER("%s %s %s", db, name, path);
  int error;
  DB *status_db = NULL;
  DB_TXN *txn = NULL;
  HA_METADATA_KEY curr_key = hatoku_frm_data;
  DBT key = {};
  DBT value = {};
  bool do_commit = false;

  error = txn_begin(db_env, 0, &txn, 0, thd);
  if (error) {
    goto cleanup;
  }
  do_commit = true;

  error = open_status_dictionary(&status_db, path, txn);
  if (error) {
    goto cleanup;
  }

  key.data = &curr_key;
  key.size = sizeof(curr_key);

  error = status_db->getf_set(status_db, txn, 0, &key,
                              smart_dbt_callback_verify_frm, &value);
  if (error) {
    goto cleanup;
  }

  *frmblob = (uchar *)value.data;
  *frmlen = value.size;

  error = 0;
cleanup:
  if (status_db) {
    status_db->close(status_db, 0);
  }
  if (do_commit && txn) {
    commit_txn(txn, 0);
  }
  TOKUDB_DBUG_RETURN(error);
}
#endif  // defined(TOKU_INCLUDE_DISCOVER_FRM) && TOKU_INCLUDE_DISCOVER_FRM

#define STATPRINT(legend, val)                                        \
  if (legend != NULL && val != NULL)                                  \
  stat_print(thd, tokudb_hton_name, strlen(tokudb_hton_name), legend, \
             strlen(legend), val, strlen(val))

extern sys_var *intern_find_sys_var(const char *str, uint length,
                                    bool no_error);

static bool tokudb_show_engine_status(THD *thd, stat_print_fn *stat_print) {
  TOKUDB_DBUG_ENTER("");
  int error;
  uint64_t panic;
  const int panic_string_len = 1024;
  char panic_string[panic_string_len] = {'\0'};
  uint64_t num_rows;
  uint64_t max_rows;
  fs_redzone_state redzone_state;
  const int bufsiz = 1024;
  char buf[bufsiz];

  error = db_env->get_engine_status_num_rows(db_env, &max_rows);
  TOKU_ENGINE_STATUS_ROW_S mystat[max_rows];
  error = db_env->get_engine_status(db_env, mystat, max_rows, &num_rows,
                                    &redzone_state, &panic, panic_string,
                                    panic_string_len, TOKU_ENGINE_STATUS);

  if (strlen(panic_string)) {
    STATPRINT("Environment panic string", panic_string);
  }
  if (error == 0) {
    if (panic) {
      snprintf(buf, bufsiz, "%" PRIu64, panic);
      STATPRINT("Environment panic", buf);
    }

    if (redzone_state == FS_BLOCKED) {
      STATPRINT("*** URGENT WARNING ***", "FILE SYSTEM IS COMPLETELY FULL");
      snprintf(buf, bufsiz, "FILE SYSTEM IS COMPLETELY FULL");
    } else if (redzone_state == FS_GREEN) {
      snprintf(buf, bufsiz, "more than %d percent of total file system space",
               2 * tokudb::sysvars::fs_reserve_percent);
    } else if (redzone_state == FS_YELLOW) {
      snprintf(buf, bufsiz,
               "*** WARNING *** FILE SYSTEM IS GETTING FULL (less than %d "
               "percent free)",
               2 * tokudb::sysvars::fs_reserve_percent);
    } else if (redzone_state == FS_RED) {
      snprintf(buf, bufsiz,
               "*** WARNING *** FILE SYSTEM IS GETTING VERY FULL (less than "
               "%d percent free): INSERTS ARE PROHIBITED",
               tokudb::sysvars::fs_reserve_percent);
    } else {
      snprintf(buf, bufsiz, "information unavailable, unknown redzone state %d",
               redzone_state);
    }
    STATPRINT("disk free space", buf);

    for (uint64_t row = 0; row < num_rows; row++) {
      switch (mystat[row].type) {
        case FS_STATE:
          snprintf(buf, bufsiz, "%" PRIu64 "", mystat[row].value.num);
          break;
        case UINT64:
          snprintf(buf, bufsiz, "%" PRIu64 "", mystat[row].value.num);
          break;
        case CHARSTR:
          snprintf(buf, bufsiz, "%s", mystat[row].value.str);
          break;
        case UNIXTIME: {
          time_t t = mystat[row].value.num;
          char tbuf[26];
          snprintf(buf, bufsiz, "%.24s", ctime_r(&t, tbuf));
          break;
        }
        case TOKUTIME: {
          double t = tokutime_to_seconds(mystat[row].value.num);
          snprintf(buf, bufsiz, "%.6f", t);
          break;
        }
        case PARCOUNT: {
          uint64_t v = read_partitioned_counter(mystat[row].value.parcount);
          snprintf(buf, bufsiz, "%" PRIu64, v);
          break;
        }
        case DOUBLE:
          snprintf(buf, bufsiz, "%.6f", mystat[row].value.dnum);
          break;
        default:
          snprintf(buf, bufsiz, "UNKNOWN STATUS TYPE: %d", mystat[row].type);
          break;
      }
      STATPRINT(mystat[row].legend, buf);
    }
    uint64_t bytes_inserted =
        read_partitioned_counter(tokudb_primary_key_bytes_inserted);
    snprintf(buf, bufsiz, "%" PRIu64, bytes_inserted);
    STATPRINT("handlerton: primary key bytes inserted", buf);
  }
  if (error) {
    set_my_errno(error);
  }
  TOKUDB_DBUG_RETURN(error);
}

void tokudb_checkpoint_lock(THD *thd) {
  int error;
  const char *old_proc_info;
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, tokudb_hton);
  if (!trx) {
    error = create_tokudb_trx_data_instance(&trx);
    //
    // can only fail due to memory allocation, so ok to assert
    //
    assert_always(!error);
    thd_set_ha_data(thd, tokudb_hton, trx);
  }

  if (trx->checkpoint_lock_taken) {
    goto cleanup;
  }
  //
  // This can only fail if environment is not created, which is not possible
  // in handlerton
  //
  old_proc_info = tokudb_thd_get_proc_info(thd);
  thd_proc_info(thd, "Trying to grab checkpointing lock.");
  error = db_env->checkpointing_postpone(db_env);
  assert_always(!error);
  thd_proc_info(thd, old_proc_info);

  trx->checkpoint_lock_taken = true;
cleanup:
  return;
}

void tokudb_checkpoint_unlock(THD *thd) {
  int error;
  const char *old_proc_info;
  tokudb_trx_data *trx = (tokudb_trx_data *)thd_get_ha_data(thd, tokudb_hton);
  if (!trx) {
    error = 0;
    goto cleanup;
  }
  if (!trx->checkpoint_lock_taken) {
    error = 0;
    goto cleanup;
  }
  //
  // at this point, we know the checkpoint lock has been taken
  //
  old_proc_info = tokudb_thd_get_proc_info(thd);
  thd_proc_info(thd, "Trying to release checkpointing lock.");
  error = db_env->checkpointing_resume(db_env);
  assert_always(!error);
  thd_proc_info(thd, old_proc_info);

  trx->checkpoint_lock_taken = false;

cleanup:
  return;
}

static bool tokudb_show_status(TOKUDB_UNUSED(handlerton *hton), THD *thd,
                               stat_print_fn *stat_print,
                               enum ha_stat_type stat_type) {
  switch (stat_type) {
    case HA_ENGINE_STATUS:
      return tokudb_show_engine_status(thd, stat_print);
      break;
    default:
      break;
  }
  return false;
}

static void tokudb_print_error(TOKUDB_UNUSED(const DB_ENV *db_env),
                               const char *db_errpfx, const char *buffer) {
  LogPluginErrMsg(ERROR_LEVEL, 0, "%s: %s", db_errpfx, buffer);
}

static void tokudb_cleanup_log_files(void) {
  TOKUDB_DBUG_ENTER("");
  char **names;
  int error;

  if ((error = db_env->txn_checkpoint(db_env, 0, 0, 0)))
    my_error(ER_ERROR_DURING_FLUSH_LOGS, MYF(0), error);

  if ((error = db_env->log_archive(db_env, &names, 0)) != 0) {
    DBUG_PRINT("error", ("log_archive failed (error %d)", error));
    db_env->err(db_env, error, "log_archive");
    DBUG_VOID_RETURN;
  }

  if (names) {
    char **np;
    for (np = names; *np; ++np) {
#if 1
      if (TOKUDB_UNLIKELY(tokudb::sysvars::debug))
        TOKUDB_TRACE("cleanup:%s", *np);
#else
      my_delete(*np, MYF(MY_WME));
#endif
    }

    free(names);
  }

  DBUG_VOID_RETURN;
}

// Split ./database/table-dictionary into database, table and dictionary strings
void tokudb_split_dname(const char *dname, String &database_name,
                        String &table_name, String &dictionary_name) {
  const char *splitter = strchr(dname, '/');
  if (splitter) {
    const char *database_ptr = splitter + 1;
    const char *table_ptr = strchr(database_ptr, '/');
    if (table_ptr) {
      database_name.append(database_ptr, table_ptr - database_ptr);
      table_ptr += 1;
      const char *dictionary_ptr = strchr(table_ptr, '-');
      if (dictionary_ptr) {
        table_name.append(table_ptr, dictionary_ptr - table_ptr);
        dictionary_ptr += 1;
        dictionary_name.append(dictionary_ptr);
      } else {
        table_name.append(table_ptr);
      }
    } else {
      database_name.append(database_ptr);
    }
  }
}

struct st_mysql_storage_engine tokudb_storage_engine = {
    MYSQL_HANDLERTON_INTERFACE_VERSION};

static void tokudb_pretty_key(const DBT *key, const char *default_key,
                              String *out) {
  if (key->data == NULL) {
    out->append(default_key);
  } else {
    bool do_hexdump = true;
    if (do_hexdump) {
      // hexdump the key
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(key->data);
      for (size_t i = 0; i < key->size; i++) {
        char str[3];
        snprintf(str, sizeof str, "%2.2x", data[i]);
        out->append(str);
      }
    }
  }
}

void tokudb_pretty_left_key(const DBT *key, String *out) {
  tokudb_pretty_key(key, "-infinity", out);
}

void tokudb_pretty_right_key(const DBT *key, String *out) {
  tokudb_pretty_key(key, "+infinity", out);
}

const char *tokudb_get_index_name(DB *db) {
  if (db != NULL) {
    return db->get_dname(db);
  } else {
    return "$ydb_internal";
  }
}

static int tokudb_equal_key(const DBT *left_key, const DBT *right_key) {
  if (left_key->data == NULL || right_key->data == NULL ||
      left_key->size != right_key->size)
    return 0;
  else
    return memcmp(left_key->data, right_key->data, left_key->size) == 0;
}

static void tokudb_lock_timeout_callback(DB *db, uint64_t requesting_txnid,
                                         const DBT *left_key,
                                         const DBT *right_key,
                                         uint64_t blocking_txnid) {
  THD *thd = current_thd;
  if (!thd) return;
  ulong lock_timeout_debug = tokudb::sysvars::lock_timeout_debug(thd);
  if (lock_timeout_debug != 0) {
    // generate a JSON document with the lock timeout info
    String log_str;
    log_str.append("{");
    my_thread_id mysql_thread_id = thd->thread_id();
    log_str.append("\"mysql_thread_id\":");
    log_str.append_ulonglong(static_cast<ulonglong>(mysql_thread_id));
    log_str.append(", \"dbname\":");
    log_str.append("\"");
    log_str.append(tokudb_get_index_name(db));
    log_str.append("\"");
    log_str.append(", \"requesting_txnid\":");
    log_str.append_ulonglong(requesting_txnid);
    log_str.append(", \"blocking_txnid\":");
    log_str.append_ulonglong(blocking_txnid);
    if (tokudb_equal_key(left_key, right_key)) {
      String key_str;
      tokudb_pretty_key(left_key, "?", &key_str);
      log_str.append(", \"key\":");
      log_str.append("\"");
      log_str.append(key_str);
      log_str.append("\"");
    } else {
      String left_str;
      tokudb_pretty_left_key(left_key, &left_str);
      log_str.append(", \"key_left\":");
      log_str.append("\"");
      log_str.append(left_str);
      log_str.append("\"");
      String right_str;
      tokudb_pretty_right_key(right_key, &right_str);
      log_str.append(", \"key_right\":");
      log_str.append("\"");
      log_str.append(right_str);
      log_str.append("\"");
    }
    log_str.append("}");
    // set last_lock_timeout
    if (lock_timeout_debug & 1) {
      char *old_lock_timeout = tokudb::sysvars::last_lock_timeout(thd);
      char *new_lock_timeout = tokudb::memory::strdup(log_str.c_ptr(), MY_FAE);
      tokudb::sysvars::set_last_lock_timeout(thd, new_lock_timeout);
#if defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
      mutex_t_lock(tokudb_map_mutex);
      struct tokudb_map_pair old_key = {thd, old_lock_timeout};
      tree_delete(&tokudb_map, &old_key, sizeof old_key, NULL);
      struct tokudb_map_pair new_key = {thd, new_lock_timeout};
      tree_insert(&tokudb_map, &new_key, sizeof new_key, NULL);
      mutex_t_unlock(tokudb_map_mutex);
#endif  // defined(TOKU_THDVAR_MEMALLOC_BUG) && TOKU_THDVAR_MEMALLOC_BUG
      tokudb::memory::free(old_lock_timeout);
    }
    // dump to stderr
    if (lock_timeout_debug & 2) {
      LogPluginErrMsg(ERROR_LEVEL, 0, "Lock timeout %s", log_str.c_ptr());
      LEX_CSTRING qs = thd->query();
      LogPluginErrMsg(ERROR_LEVEL, 0, "Requesting_thread_id:%" PRIu64 " q:%.*s",
                      static_cast<uint64_t>(mysql_thread_id), (int)qs.length,
                      qs.str);
    }
  }
}

// Retrieves variables for information_schema.global_status.
// Names (columnname) are automatically converted to upper case,
// and prefixed with "TOKUDB_"
static int show_tokudb_vars(TOKUDB_UNUSED(THD *thd), SHOW_VAR *var,
                            TOKUDB_UNUSED(char *buff)) {
  TOKUDB_DBUG_ENTER("");

  int error;
  uint64_t panic;
  const int panic_string_len = 1024;
  char panic_string[panic_string_len] = {'\0'};
  fs_redzone_state redzone_state;

  uint64_t num_rows;
  error = db_env->get_engine_status(db_env, toku_global_status_rows,
                                    toku_global_status_max_rows, &num_rows,
                                    &redzone_state, &panic, panic_string,
                                    panic_string_len, TOKU_GLOBAL_STATUS);
  // TODO: Maybe do something with the panic output?
  if (error == 0) {
    assert_always(num_rows <= toku_global_status_max_rows);
    // TODO: Maybe enable some of the items here: (copied from engine status

    // TODO: (optionally) add redzone state, panic, panic string, etc.
    // Right now it's being ignored.

    for (uint64_t row = 0; row < num_rows; row++) {
      SHOW_VAR &status_var = toku_global_status_variables[row];
      TOKU_ENGINE_STATUS_ROW_S &status_row = toku_global_status_rows[row];

      status_var.name = status_row.columnname;
      status_var.scope = SHOW_SCOPE_GLOBAL;
      switch (status_row.type) {
        case FS_STATE:
        case UINT64:
          status_var.type = SHOW_LONGLONG;
          status_var.value = (char *)&status_row.value.num;
          break;
        case CHARSTR:
          status_var.type = SHOW_CHAR;
          status_var.value = (char *)status_row.value.str;
          break;
        case UNIXTIME: {
          status_var.type = SHOW_CHAR;
          time_t t = status_row.value.num;
          char tbuf[26];
          // Reuse the memory in status_row. (It belongs to us).
          snprintf(status_row.value.datebuf, sizeof(status_row.value.datebuf),
                   "%.24s", ctime_r(&t, tbuf));
          status_var.value = (char *)&status_row.value.datebuf[0];
          break;
        }
        case TOKUTIME:
          status_var.type = SHOW_DOUBLE;
          // Reuse the memory in status_row. (It belongs to us).
          status_row.value.dnum = tokutime_to_seconds(status_row.value.num);
          status_var.value = (char *)&status_row.value.dnum;
          break;
        case PARCOUNT: {
          status_var.type = SHOW_LONGLONG;
          uint64_t v = read_partitioned_counter(status_row.value.parcount);
          // Reuse the memory in status_row. (It belongs to us).
          status_row.value.num = v;
          status_var.value = (char *)&status_row.value.num;
          break;
        }
        case DOUBLE:
          status_var.type = SHOW_DOUBLE;
          status_var.value = (char *)&status_row.value.dnum;
          break;
        default:
          status_var.type = SHOW_CHAR;
          // Reuse the memory in status_row.datebuf. (It belongs to
          // us). UNKNOWN TYPE: %d fits in 26 bytes (sizeof datebuf)
          // for any integer.
          snprintf(status_row.value.datebuf, sizeof(status_row.value.datebuf),
                   "UNKNOWN TYPE: %d", status_row.type);
          status_var.value = (char *)&status_row.value.datebuf[0];
          break;
      }
    }
    // Sentinel value at end.
    toku_global_status_variables[num_rows].scope = SHOW_SCOPE_GLOBAL;
    toku_global_status_variables[num_rows].type = SHOW_LONG;
    toku_global_status_variables[num_rows].value = (char *)NullS;
    toku_global_status_variables[num_rows].name = (char *)NullS;

    var->type = SHOW_ARRAY;
    var->value = (char *)toku_global_status_variables;
    var->scope = SHOW_SCOPE_GLOBAL;
  }
  if (error) {
    set_my_errno(error);
  }
  TOKUDB_DBUG_RETURN(error);
}

static SHOW_VAR toku_global_status_variables_export[] = {
    {"Tokudb", (char *)&show_tokudb_vars, SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}};

mysql_declare_plugin(tokudb){
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &tokudb_storage_engine,
    tokudb_hton_name,
    "Percona",
    "Percona TokuDB Storage Engine with Fractal Tree(tm) Technology",
    PLUGIN_LICENSE_GPL,
    tokudb_init_func, /* plugin init */
    nullptr,          /* plugin check uninstall */
    tokudb_done_func, /* plugin deinit */
    TOKUDB_PLUGIN_VERSION,
    toku_global_status_variables_export, /* status variables */
    tokudb::sysvars::system_variables,   /* system variables */
    NULL,                                /* config options */
    0,                                   /* flags */
},
    tokudb::information_schema::trx, tokudb::information_schema::lock_waits,
    tokudb::information_schema::locks, tokudb::information_schema::file_map,
    tokudb::information_schema::fractal_tree_info,
    tokudb::information_schema::fractal_tree_block_map,
    tokudb::information_schema::background_job_status mysql_declare_plugin_end;
