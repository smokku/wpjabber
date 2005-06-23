/* --------------------------------------------------------------------------
 *
 *  Oracle support routines for xdb_sql
 *
 *  Developed by Mike Shoyher <mike@shoyher.com> http://www.shoyher.com
 *
 *  Usage, modification and distribution rights reserved. See file
 *  COPYING for details.
 *
 * --------------------------------------------------------------------------*/
#ifndef XDB_SQL_ORACLE_H_INCLUDED
#define XDB_SQL_ORACLE_H_INCLUDED

#include "xdb_sql_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <oratypes.h>
#include <oci.h>



XdbSqlBackend *xdboracle_backend_new (void);






#endif /* XDB_SQL_ORACLE_H_INCLUDED */
