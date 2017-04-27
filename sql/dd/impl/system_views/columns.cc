/* Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "dd/impl/system_views/columns.h"

namespace dd {
namespace system_views {

const Columns &Columns::instance()
{
  static Columns *s_instance= new Columns();
  return *s_instance;
}

Columns::Columns()
{
  m_target_def.set_view_name(view_name());

  m_target_def.add_field(FIELD_TABLE_CATALOG, "TABLE_CATALOG",
                        "cat.name" + m_target_def.fs_name_collation());
  m_target_def.add_field(FIELD_TABLE_SCHEMA, "TABLE_SCHEMA",
                        "sch.name" + m_target_def.fs_name_collation());
  m_target_def.add_field(FIELD_TABLE_NAME, "TABLE_NAME",
                        "tbl.name" + m_target_def.fs_name_collation());
  m_target_def.add_field(FIELD_COLUMN_NAME, "COLUMN_NAME",
                        "col.name COLLATE utf8_tolower_ci");
  m_target_def.add_field(FIELD_ORDINAL_POSITION, "ORDINAL_POSITION",
                        "col.ordinal_position");
  m_target_def.add_field(FIELD_COLUMN_DEFAULT, "COLUMN_DEFAULT",
                        "col.default_value_utf8");
  m_target_def.add_field(FIELD_IS_NULLABLE, "IS_NULLABLE",
                        "IF (col.is_nullable = 1, 'YES','NO')");
  m_target_def.add_field(FIELD_DATA_TYPE, "DATA_TYPE",
      "SUBSTRING_INDEX(SUBSTRING_INDEX(col.column_type_utf8, '(', 1),' ', 1)");
  m_target_def.add_field( FIELD_CHARACTER_MAXIMUM_LENGTH,
    "CHARACTER_MAXIMUM_LENGTH",
    "INTERNAL_DD_CHAR_LENGTH(col.type, col.char_length, coll.name, 0)");
  m_target_def.add_field(FIELD_CHARACTER_OCTET_LENGTH, "CHARACTER_OCTET_LENGTH",
    "INTERNAL_DD_CHAR_LENGTH(col.type, col.char_length, coll.name, 1)");
  m_target_def.add_field(FIELD_NUMERIC_PRECISION, "NUMERIC_PRECISION",
    "IF (col.numeric_precision = 0, NULL, col.numeric_precision)");
  m_target_def.add_field(FIELD_NUMERIC_SCALE, "NUMERIC_SCALE",
    "IF (col.numeric_scale = 0 && col.numeric_precision = 0,"
    "    NULL, col.numeric_scale)");
  m_target_def.add_field(FIELD_DATETIME_PRECISION, "DATETIME_PRECISION",
                        "col.datetime_precision");
  m_target_def.add_field(FIELD_CHARACTER_SET_NAME, "CHARACTER_SET_NAME",
    "CASE col.type"
    "  WHEN 'MYSQL_TYPE_STRING' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_VAR_STRING' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_VARCHAR' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_TINY_BLOB' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_MEDIUM_BLOB' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_BLOB' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_LONG_BLOB' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_ENUM' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  WHEN 'MYSQL_TYPE_SET' THEN (IF (cs.name='binary',NULL, cs.name))"
    "  ELSE NULL "
    "END");
  m_target_def.add_field(FIELD_COLLATION_NAME, "COLLATION_NAME",
    "CASE col.type"
    "  WHEN 'MYSQL_TYPE_STRING' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_VAR_STRING' THEN "
    "    (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_VARCHAR' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_TINY_BLOB' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_MEDIUM_BLOB' THEN "
    "    (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_BLOB' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_LONG_BLOB' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_ENUM' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  WHEN 'MYSQL_TYPE_SET' THEN (IF (cs.name='binary',NULL, coll.name))"
    "  ELSE NULL "
    "END");
  m_target_def.add_field(FIELD_COLUMN_TYPE, "COLUMN_TYPE",
                        "col.column_type_utf8");
  m_target_def.add_field(FIELD_COLUMN_KEY, "COLUMN_KEY",
                        "col.column_key");
  m_target_def.add_field(FIELD_EXTRA, "EXTRA",
    "IF(IFNULL(col.generation_expression_utf8,'IS_NOT_GC')='IS_NOT_GC',"
    "   IF (col.is_auto_increment=TRUE,"
    "        CONCAT(IFNULL(CONCAT('on update ', col.update_option, ' '),''),"
    "                'auto_increment'),"
    "       IFNULL(CONCAT('on update ', col.update_option),'')),"
    "  IF(col.is_virtual, 'VIRTUAL GENERATED', 'STORED GENERATED'))");
  m_target_def.add_field(FIELD_PRIVILEGES, "PRIVILEGES",
    "GET_DD_COLUMN_PRIVILEGES(sch.name, tbl.name, col.name)");
  m_target_def.add_field(FIELD_COLUMN_COMMENT, "COLUMN_COMMENT",
                        "IFNULL(col.comment, '')");
  m_target_def.add_field(FIELD_GENERATION_EXPRESSION, "GENERATION_EXPRESSION",
                        "IFNULL(col.generation_expression_utf8, '')");

  m_target_def.add_from("mysql.columns col");
  m_target_def.add_from("JOIN mysql.tables tbl ON col.table_id=tbl.id");
  m_target_def.add_from("JOIN mysql.schemata sch ON tbl.schema_id=sch.id");
  m_target_def.add_from("JOIN mysql.catalogs cat ON cat.id=sch.catalog_id");
  m_target_def.add_from("JOIN mysql.collations coll "
                        "ON col.collation_id=coll.id");
  m_target_def.add_from("JOIN mysql.character_sets cs "
                        "ON coll.character_set_id= cs.id");

  m_target_def.add_where("INTERNAL_GET_VIEW_WARNING_OR_ERROR(sch.name,"
                         "tbl.name, tbl.type, tbl.options)");
  m_target_def.add_where("AND CAN_ACCESS_COLUMN(sch.name, tbl.name, col.name, col.hidden)");
  m_target_def.add_where("AND tbl.hidden = 'Visible'");
}

}
}