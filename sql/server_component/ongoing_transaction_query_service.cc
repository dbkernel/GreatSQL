/* Copyright (c) 2018, 2021, Oracle and/or its affiliates.
   Copyright (c) 2022, GreatDB Software Co., Ltd

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <sql/current_thd.h>
#include <sql/mysqld_thd_manager.h>
#include <sql/sql_lex.h>
#include "mysql/plugin.h"
#include "mysql_ongoing_transaction_query_imp.h"
#include "sql/sql_class.h"  // THD

#define ALL_ACTIVE_TRX 0
#define COMMITTING_TRX 1
#define ISO_SERIALIZABLE_TRX 2
#define CASCADE_TABLE_TRX 4

class Get_running_transactions : public Do_THD_Impl {
 public:
  Get_running_transactions(int trx_mode) : m_trx_mode(trx_mode) {}

  /*
   This method relies on the assumption that a thread running query will either
   have an active query plan, or is in the middle of a multi statement
   transaction.
  */
  void operator()(THD *thd) override {
    if (thd->is_killed() || thd->is_error()) return;

    TX_TRACKER_GET(tst);

    /*
      Show we're at least as restrictive detecting transactions as the
      original code for BUG#28327838 that we're replacing!!
    */
    assert(((tst->get_trx_state() & TX_EXPLICIT) > 0) >=
           (thd->in_active_multi_stmt_transaction() > 0));

    /*
      Show we're detecting DML at least in all cases the original code does.

      NB  TX_STMT_DML starts off false, and is turned on if after parsing, the
          statement self-identifies as DML.  This specifically means that for
          scenarios that somehow don't actually go through the parser, or
          haven't gone through the parser yet at the time of examining, results
          will differ between these two approaches. For the sake of this
          assertion though, we'll pass it if TX_STMT_DML is not set as long as
          no query is set on that THD, either.
    */

    bool chosen = false;
    if (!(tst->get_trx_state() & (TX_EXPLICIT | TX_STMT_DML))) {
      return;
    }

    if (m_trx_mode == ALL_ACTIVE_TRX) {
      chosen = true;
    }
    if (m_trx_mode & COMMITTING_TRX) {
      if (tst->get_trx_state() & (TX_BEFORE_COMMIT)) {
        chosen = true;
      }
    }
    if (m_trx_mode & ISO_SERIALIZABLE_TRX) {
      auto iso = thd_tx_isolation(thd);
      if (iso == ISO_SERIALIZABLE) {
        chosen = true;
      }
    }
    if (m_trx_mode & CASCADE_TABLE_TRX) {
      if (tst->get_trx_table_flag() & (TX_TABLE_CASCADE)) {
        chosen = true;
      }
    }
    if (chosen) {
      thread_ids.push_back(thd->thread_id());
    }
  }

  ulong get_transaction_number() { return thread_ids.size(); }

  void fill_transaction_ids(unsigned long **ids) {
    size_t number_thd = thread_ids.size();
    *ids = (unsigned long *)my_malloc(
        PSI_NOT_INSTRUMENTED, number_thd * sizeof(unsigned long), MYF(MY_WME));
    int index = 0;
    for (std::vector<my_thread_id>::iterator it = thread_ids.begin();
         it != thread_ids.end(); ++it) {
      (*ids)[index] = *it;
      index++;
    }
  }

 private:
  /* Status of all threads are summed into this. */
  std::vector<my_thread_id> thread_ids;
  int m_trx_mode;
};

DEFINE_BOOL_METHOD(
    mysql_ongoing_transactions_query_imp::get_ongoing_server_transactions,
    (unsigned long **thread_ids, unsigned long *lenght)) {
  Get_running_transactions trx_counter(ALL_ACTIVE_TRX);
  Global_THD_manager::get_instance()->do_for_all_thd(&trx_counter);
  trx_counter.fill_transaction_ids(thread_ids);
  *lenght = trx_counter.get_transaction_number();
  return false;
}
DEFINE_BOOL_METHOD(mysql_ongoing_transactions_query_imp::
                       get_group_replication_waitting_transactions,
                   (unsigned long **thread_ids, unsigned long *lenght)) {
  Get_running_transactions trx_counter(COMMITTING_TRX | ISO_SERIALIZABLE_TRX |
                                       CASCADE_TABLE_TRX);
  Global_THD_manager::get_instance()->do_for_all_thd(&trx_counter);
  trx_counter.fill_transaction_ids(thread_ids);
  *lenght = trx_counter.get_transaction_number();
  return false;
}
