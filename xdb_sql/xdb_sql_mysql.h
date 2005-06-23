/* --------------------------------------------------------------------------
 *
 *  This program was developed by IDEALX (http://www.IDEALX.org), for
 *  LibertySurf Télécom (http://www.libertysurftelecom.fr), based on
 *  code developped by members of the Jabber Team in xdb/mysql.
 *
 *  Initial release in xdb_sql 1.0, (C) 2000 - 2001 LibertySurf
 *  Télécom
 *
 *  Individual authors' names can be found in the AUTHORS file
 *
 *  Usage, modification and distribution rights reserved. See file
 *  COPYING for details.
 *
 * --------------------------------------------------------------------------*/
#ifndef XDB_SQL_MYSQL_H_INCLUDED
#define XDB_SQL_MYSQL_H_INCLUDED

#include "xdb_sql_backend.h"
#include <mysql.h>

XdbSqlBackend *xdbmysql_backend_new (void);

#endif /* XDB_SQL_MYSQL_H_INCLUDED */
