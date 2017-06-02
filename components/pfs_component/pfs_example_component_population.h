/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1307  USA */

#ifndef PFS_EXAMPLE_COMPONENT_H
#define PFS_EXAMPLE_COMPONENT_H

#include <mysql/components/component_implementation.h>
#include <mysql/components/service_implementation.h>
#include <mysql/components/services/pfs_plugin_table_service.h>

/* A place to specify component-wide declarations, including declarations of
  placeholders for Service dependencies. */

extern REQUIRES_SERVICE_PLACEHOLDER(pfs_plugin_table);

#endif /* PFS_EXAMPLE_COMPONENT_H */