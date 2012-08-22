#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <libpq-fe.h>
#include <log.h>
#include <mem.h>
#include <util.h>
#include <mstring.h>
#include <time.h>
#include <sys/time.h>
#include "database.h"

extern char *pg_conninfo;
extern char *pg_user;

typedef struct _psqlDB {
  PGconn *conn;
  int sender_cnt;
  time_t last_commit;
} PsqlDB;

typedef struct _pqlTable {
  MString *insert_stmt; /* Named statement for inserting into this table */
} PsqlTable;

static int sql_stmt(PsqlDB* self, const char* stmt);
char* psql_get_key_value (Database *database, const char *table,
                          const char *key_column, const char *value_column,
                          const char *key);
int psql_set_key_value (Database *database, const char *table,
                        const char *key_column, const char *value_column,
                        const char *key, const char *value);

static int
begin_transaction (PsqlDB *db)
{
  const char *sql = "BEGIN TRANSACTION;";
  return sql_stmt (db, sql);
}

static int
end_transaction (PsqlDB *db)
{
  const char *sql = "END TRANSACTION";
  return sql_stmt (db, sql);
}

static int
reopen_transaction (PsqlDB *db)
{
  if (end_transaction (db) == -1) return -1;
  if (begin_transaction (db) == -1) return -1;
  return 0;
}

static MString*
psql_make_sql_insert (DbTable* table)
{
  int n = 0;
  int max = table->schema->nfields;

  if (max <= 0) {
    logerror ("psql: Trying to insert 0 values into table '%s'\n", table->schema->name);
    goto fail_exit;
  }

  MString* mstr = mstring_create ();

  if (mstr == NULL) {
    logerror("psql: Failed to create managed string for preparing SQL INSERT statement\n");
    goto fail_exit;
  }

  /* Build SQL "INSERT INTO" statement */
  n += mstring_set (mstr, "INSERT INTO \"");
  n += mstring_cat (mstr, table->schema->name);
  n += mstring_cat (mstr, "\" VALUES ($1, $2, $3, $4"); /* metadata columns */
  while (max-- > 0)
    mstring_sprintf (mstr, ", $%d", 4 + table->schema->nfields - max);
  mstring_cat (mstr, ");");

  if (n != 0) goto fail_exit;
  return mstr;

 fail_exit:
  if (mstr) mstring_delete (mstr);
  return NULL;
}

static struct {
  const char *name;
  const char *sql;
} meta_tables [] = {
  { .name = "_experiment_metadata",
    .sql = "CREATE TABLE _experiment_metadata (key TEXT PRIMARY KEY, value TEXT);" },
  { .name = "_senders",
    .sql = "CREATE TABLE _senders (name TEXT PRIMARY KEY, id INTEGER UNIQUE);" },
};


static int
psql_table_create_meta (Database *db, const char *name)
{
  PsqlDB *self = (PsqlDB*)db->handle;
  size_t i = 0;
  for (i = 0; i < LENGTH (meta_tables); i++)
    if (strcmp (meta_tables[i].name, name) == 0)
      return sql_stmt (self, meta_tables[i].sql);
  return -1;
}

/**
 * @brief Release the psql database.
 *
 * This function closes the connection to the database and frees all
 * of the allocated memory associated with the database.
 *
 * @param db the database that contains the psql database to release.
 */
void
psql_release(Database* db)
{
  PsqlDB* self = (PsqlDB*)db->handle;
  end_transaction (self);
  PQfinish(self->conn);
  // TODO: Release table specific data

  xfree(self);
  db->handle = NULL;
}

static char*
psql_get_sender_id (Database *database, const char *name)
{
  return psql_get_key_value (database, "_senders", "name", "id", name);
}

static int
psql_set_sender_id (Database *database, const char *name, int id)
{
  MString *mstr = mstring_create();
  mstring_sprintf (mstr, "%d", id);
  int ret = psql_set_key_value (database, "_senders", "name", "id", name, mstring_buf (mstr));
  mstring_delete (mstr);
  return ret;
}

/**
 * @brief Add sender a new sender to the database, returning its
 * index.
 *
 * If a sender with the given id already exists, its pre-existing
 * index is returned.  Otherwise, a new sender is added to the table
 * with a new sender id, unique to this experiment.
 *
 * @param db the database that contains the experiment db
 * @param sender_id the sender ID
 * @return the index of the sender
 */
static int
psql_add_sender_id(Database *db, char *sender_id)
{
  PsqlDB *self = (PsqlDB*)db->handle;
  int index = -1;
  char *id_str = psql_get_sender_id (db, sender_id);

  if (id_str) {
    index = atoi (id_str);
    xfree (id_str);

  } else {
    PGresult *res = PQexec (self->conn, "SELECT MAX(id) FROM _senders;");

    if (PQresultStatus (res) != PGRES_TUPLES_OK) {
      logwarn("psql:%s: Failed to get maximum sender id from database: %s; starting at 0\n",
          db->name, PQerrorMessage (self->conn));
      PQclear (res);
      return -1;
    }
    int rows = PQntuples (res);
    if (rows == 0) {
      logerror("psql:%s: Failed to get maximum sender id from database: empty result\n",
            db->name);
      PQclear (res);
      return -1;
    }
    index = atoi (PQgetvalue (res, 0, 0)) + 1;
    PQclear (res);
    psql_set_sender_id (db, sender_id, index);

  }

  return index;
}

char*
psql_get_metadata (Database *db, const char *key)
{
  return psql_get_key_value (db, "_experiment_metadata", "key", "value", key);
}

int
psql_set_metadata (Database *db, const char *key, const char *value)
{
  return psql_set_key_value (db, "_experiment_metadata", "key", "value", key, value);
}

/**
 *  @brief Do a key-value style select on a database table.
 *
 *  This function does a key lookup on a database table that is set up
 *  in key-value style.  The table can have more than two columns, but
 *  this function SELECT's two of them and returns the value of the
 *  value column.  It checks to make sure that the key returned is the
 *  one requested, then returns its corresponding value.
 *
 *  This function makes a lot of assumptions about the database and
 *  the table:
 *
 *  #- the database exists and is open
 *  #- the table exists in the database
 *  #- there is a column named key_column in the table
 *  #- there is a column named value_column in the table
 *
 *  The function does not check for any of these conditions, but just
 *  assumes they are true.  Be advised.
 *
 *  @param database the database to look up in.
 *  @param table the name of the table to look up in.
 *  @param key_column the name of the column holding the key strings.
 *  @param value_column the name of the column holding the value strings.
 *  @param key the key string to look up (i.e. WHERE key_column='key').
 *
 *  @return the string value corresponding to the given key, or NULL
 *  if an error occurred of if the key was not present in the table.
 */
char*
psql_get_key_value (Database *database, const char *table,
                    const char *key_column, const char *value_column,
                    const char *key)
{
  if (database == NULL || table == NULL || key_column == NULL ||
      value_column == NULL || key == NULL)
    return NULL;

  PGresult *res;
  PsqlDB *psqldb = (PsqlDB*) database->handle;
  MString *stmt = mstring_create();
  mstring_sprintf (stmt, "SELECT %s FROM %s WHERE %s='%s';",
                   value_column, table, key_column, key);

  res = PQexec (psqldb->conn, mstring_buf (stmt));

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    logerror("psql:%s: Error trying to get %s[%s]; (%s).\n",
             database->name, table, key, PQerrorMessage(psqldb->conn));
    goto fail_exit;
  }

  if (PQntuples (res) == 0)
    goto fail_exit;
  if (PQnfields (res) < 1)
    goto fail_exit;

  if (PQntuples (res) > 1)
    logwarn("psql:%s: Key-value lookup for key '%s' in %s(%s, %s) returned more than one possible key.\n",
             database->name, key, table, key_column, value_column);

  char *value = NULL;
  value = PQgetvalue (res, 0, 0);

  if (value != NULL)
    value = xstrndup (value, strlen (value));

  PQclear (res);
  mstring_delete (stmt);
  return value;

 fail_exit:
  PQclear (res);
  mstring_delete (stmt);
  return NULL;
}

int
psql_set_key_value (Database *database, const char *table,
                    const char *key_column, const char *value_column,
                    const char *key, const char *value)
{
  PsqlDB *psqldb = (PsqlDB*) database->handle;
  MString *stmt = mstring_create ();
  char *check_value = psql_get_key_value (database, table, key_column, value_column, key);
  if (check_value == NULL)
    mstring_sprintf (stmt, "INSERT INTO \"%s\" (\"%s\", \"%s\") VALUES ('%s', '%s');",
                     table, key_column, value_column, key, value);
  else
    mstring_sprintf (stmt, "UPDATE \"%s\" SET \"%s\"='%s' WHERE \"%s\"='%s';",
                     table, value_column, value, key_column, key);

  if (sql_stmt (psqldb, mstring_buf (stmt))) {
    logwarn("psql:%s: Key-value update failed for %s='%s' in %s(%s, %s) (database error)\n",
            database->name, key, value, table, key_column, value_column);
    return -1;
  }

  return 0;
}

/**
 * @brief Create a PostgreSQL table
 * @param db the database that contains the sqlite3 db
 * @param table the table to associate in sqlite3 database
 * @return 0 if successful, -1 otherwise
 */
static int
table_create (Database* db, DbTable* table, int backend_create)
{
  if (db == NULL) {
    logerror("psql: Tried to create a table in a NULL database\n");
    return -1;
  }
  if (table == NULL) {
    logerror("psql:%s: Tried to create a table from a NULL definition\n", db->name);
    return -1;
  }
  if (table->schema == NULL) {
    logerror("psql:%s: No schema defined for table, cannot create\n", db->name);
    return -1;
  }
  MString *insert = NULL, *create = NULL;
  PsqlDB* psqldb = (PsqlDB*)db->handle;
  PGresult *res;

  if (backend_create) {
    int sindex = table->schema->index;
    table->schema->index = -1;
    MString *mstr = mstring_create ();
    mstring_sprintf (mstr, "table_%s", table->schema->name);
    const char *meta = schema_to_meta (table->schema);
    table->schema->index = sindex;
    logdebug("psql:%s: SET META: %s\n", db->name, meta);
    psql_set_metadata (db, mstring_buf (mstr), meta);
    create = schema_to_sql (table->schema, oml_to_postgresql_type);
    if (!create) {
      logerror("psql:%s: Failed to build SQL CREATE TABLE statement string for table '%s'\n",
          db->name, table->schema->name);
      goto fail_exit;
    }
    if (sql_stmt (psqldb, mstring_buf (create))) {
      logerror("psql:%s: Could not create table '%s': %s\n",
          db->name, table->schema->name,
          PQerrorMessage (psqldb->conn));
      goto fail_exit;
    }
  }

  insert = psql_make_sql_insert (table);
  if (!insert) {
    logerror("psql:%s: Failed to build SQL INSERT INTO statement for table '%s'\n",
          db->name, table->schema->name);
    goto fail_exit;
  }
  /* Prepare the insert statement and update statement  */
  PsqlTable* psqltable = (PsqlTable*)xmalloc(sizeof(PsqlTable));
  table->handle = psqltable;

  MString *insert_name = mstring_create();
  mstring_set (insert_name, "OMLInsert-");
  mstring_cat (insert_name, table->schema->name);
  res = PQprepare(psqldb->conn,
                  mstring_buf (insert_name),
                  mstring_buf (insert),
                  table->schema->nfields + 4, // FIXME:  magic number of metadata cols
                  NULL);

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    logerror("psql:%s: Could not prepare statement: %s\n",
        db->name, PQerrorMessage(psqldb->conn));
    PQclear(res);
    goto fail_exit;
    return -1;
  }
  PQclear(res);
  psqltable->insert_stmt = insert_name;

  if (create) mstring_delete (create);
  if (insert) mstring_delete (insert);
  return 0;

fail_exit:
  if (create) mstring_delete (create);
  if (insert) mstring_delete (insert);
  return -1;
}

int
psql_table_create (Database *database, DbTable *table, int shallow)
{
  logdebug("psql:%s: Creating table '%s' (shallow=%d)\n", database->name, table->schema->name, shallow);
  return table_create (database, table, !shallow);
}

int
psql_table_free (Database *database, DbTable *table)
{
  (void)database;
  PsqlTable *psqltable = (PsqlTable*)table->handle;
  if (psqltable) {
    mstring_delete (psqltable->insert_stmt);
    xfree (psqltable);
  }
  return 0;
}

TableDescr*
psql_get_table_list (Database *database, int *num_tables)
{
  PsqlDB *self = database->handle;
  const char *stmt_tablename =
    "SELECT tablename FROM pg_tables WHERE tablename NOT LIKE 'pg%' AND tablename NOT LIKE 'sql%';";
  PGresult *res;
  TableDescr *tables = NULL;
  int rows, cols, i;

  *num_tables = -1;
  res = PQexec (self->conn, stmt_tablename);
  if (PQresultStatus (res) != PGRES_TUPLES_OK) {
    logerror("psql:%s: Couldn't get list of tables: %s\n",
        database->name, PQerrorMessage (self->conn));
    PQclear (res);
    return NULL;
  }
  rows = PQntuples (res);
  cols = PQnfields (res);

  if (cols < 1)
    return NULL;

  int have_meta = 0;
  for (i = 0; i < rows && !have_meta; i++)
    if (strcmp (PQgetvalue (res, i, 0), "_experiment_metadata") == 0)
      have_meta = 1;

  if(!have_meta)
    logdebug("psql:%s: No metadata found\n", database->name);

  *num_tables = 0;

  for (i = 0; i < rows; i++) {
    char *val = PQgetvalue (res, i, 0);
    logdebug("psql:%s: Found table '%s'", database->name, val);
    MString *str = mstring_create ();
    TableDescr *t = NULL;

    if (have_meta) {
      mstring_sprintf (str, "SELECT value FROM _experiment_metadata WHERE key='table_%s';", val);
      PGresult *schema_res = PQexec (self->conn, mstring_buf (str));
      if (PQresultStatus (schema_res) != PGRES_TUPLES_OK) {
        logdebug("psql:%s: Couldn't get schema for table '%s': %s; skipping\n",
            database->name, val, PQerrorMessage (self->conn));
        mstring_delete (str);
        continue;
      }
      int rows = PQntuples (schema_res);
      if (rows == 0) {
        logdebug("psql:%s: Metadata for table '%s' found but empty\n", database->name, val);
        t = table_descr_new (val, NULL); // Don't know the schema for this table
      } else {
        logdebug("psql:%s: Stored schema for table '%s': %s\n", database->name, val, PQgetvalue (schema_res, 0, 0));
        struct schema *schema = schema_from_meta (PQgetvalue (schema_res, 0, 0));
        t = table_descr_new (val, schema);
      }
      PQclear (schema_res);
      mstring_delete (str);
    } else {
      t = table_descr_new (val, NULL);
    }

    if (t) {
      t->next = tables;
      tables = t;
      (*num_tables)++;
    }
  }

  return tables;
}

/**
 * \brief Insert value in the sqlite3 database
 * \param db the database that contains the sqlite3 db
 * \param table the table to insert data in
 * \param sender_id the sender ID
 * \param seq_no the sequence number
 * \param time_stamp the timestamp of the receiving data
 * \param values the values to insert
 * \param value_count the number of values
 * \return 0 if successful, -1 otherwise
 */
static int
psql_insert(Database* db,
            DbTable*  table,
            int       sender_id,
            int       seq_no,
            double    time_stamp,
            OmlValue* values,
            int       value_count)
{
  PsqlDB* psqldb = (PsqlDB*)db->handle;
  PsqlTable* psqltable = (PsqlTable*)table->handle;
  PGresult* res;
  int i;
  double time_stamp_server;
  const char* insert_stmt = mstring_buf (psqltable->insert_stmt);

  char * paramValues[4+value_count];
  for (i=0;i<4+value_count;i++) {
    paramValues[i] = malloc(512*sizeof(char));
  }

  int paramLength[4+value_count];
  int paramFormat[4+value_count];

  sprintf(paramValues[0],"%i",sender_id);
  paramLength[0] = 0;
  paramFormat[0] = 0;

  sprintf(paramValues[1],"%i",seq_no);
  paramLength[1] = 0;
  paramFormat[1] = 0;

  sprintf(paramValues[2],"%.8f",time_stamp);
  paramLength[2] = 0;
  paramFormat[2] = 0;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_stamp_server = tv.tv_sec - db->start_time + 0.000001 * tv.tv_usec;

  if (tv.tv_sec > psqldb->last_commit) {
    if (reopen_transaction (psqldb) == -1)
      return -1;
    psqldb->last_commit = tv.tv_sec;
  }

  sprintf(paramValues[3],"%.8f",time_stamp_server);
  paramLength[3] = 0;
  paramFormat[3] = 0;

  OmlValue* v = values;
  for (i = 0; i < value_count; i++, v++) {
    struct schema_field *field = &table->schema->fields[i];
    if (v->type != field->type) {
      logerror("psql:%s: Value %d type mismatch for table '%s'\n", db->name, i, table->schema->name);
      return -1;
    }
    switch (field->type) {
    case OML_LONG_VALUE: sprintf(paramValues[4+i],"%i",(int)v->value.longValue); break;
    case OML_INT32_VALUE:  sprintf(paramValues[4+i],"%" PRId32,v->value.int32Value); break;
    case OML_UINT32_VALUE: sprintf(paramValues[4+i],"%" PRIu32,v->value.uint32Value); break;
    case OML_INT64_VALUE:  sprintf(paramValues[4+i],"%" PRId64,v->value.int64Value); break;
//    case OML_UINT64_VALUE: sprintf(paramValues[4+i],"%i" PRIu64,(int)v->value.uint64Value); break;
    case OML_DOUBLE_VALUE: sprintf(paramValues[4+i],"%.8f",v->value.doubleValue); break;
    case OML_STRING_VALUE: sprintf(paramValues[4+i],"%s",v->value.stringValue.ptr); break;
      //    case OML_BLOB_VALUE: sprintf(paramValues[4+i],"%s",v->value.blobValue.ptr); break;
    default:
      logerror("psql:%s: Unknown type %d in col '%s' of table '%s'; this is probably a bug\n",
          db->name, field->type, field->name, table->schema->name);
      return -1;
    }
    paramLength[4+i] = 0;
    paramFormat[4+i] = 0;
  }
  /* Use stuff from http://www.postgresql.org/docs/current/static/plpgsql-control-structures.html#PLPGSQL-ERROR-TRAPPING */

  res = PQexecPrepared(psqldb->conn, insert_stmt,
                       4+value_count, (const char**)paramValues,
                       (int*) &paramLength, (int*) &paramFormat, 0 );

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    logerror("psql:%s: INSERT INTO '%s' failed: %s\n",
        db->name, table->schema->name, PQerrorMessage(psqldb->conn));
    PQclear(res);
    return -1;
  }
  PQclear(res);

  for (i=0;i<4+value_count;i++) {
    free(paramValues[i]);
  }

  return 0;
}

/**
 * @brief Execute an SQL statement (using PQexec()).
 *
 * This function executes a statement with the assumption that the
 * result can be ignored; that is, it's not useful for SELECT
 * statements.
 *
 * @param self the database handle.
 * @param stmt the SQL statement to execute.
 * @return 0 if successful, -1 if the database reports an error.
 */
static int
sql_stmt(PsqlDB* self, const char* stmt)
{
  PGresult   *res;
  logdebug("psql: Will execute '%s'\n", stmt);
  res = PQexec(self->conn, stmt);

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    logerror("psql: Error executing '%s': %s\n", stmt, PQerrorMessage(self->conn));
    PQclear(res);
    return -1;
  }
  /*
   * Should PQclear PGresult whenever it is no longer needed to avoid memory
   * leaks
   */
  PQclear(res);

  return 0;
}

/**
 * @brief Receives notices from Postgre and post them as an OML log message
 * @param arg application-specific state (in our case, the table name)
 * @param res a PGRES_NONFATAL_ERROR PGresult which can be used with PQresultErrorField and PQresultErrorMessage
 * @return something not documented in the Postgre manual
 */
static void
psql_receive_notice(void *arg, const PGresult *res)
{
  switch(*PQresultErrorField(res, PG_DIAG_SEVERITY)) {
  case 'E': /*RROR*/
  case 'F': /*ATAL*/
  case 'P': /*ANIC*/
    logerror("psql:%s': %s", (char*)arg, PQresultErrorMessage(res));
    break;
  case 'W': /*ARNING*/
    logwarn("psql:%s': %s", (char*)arg, PQresultErrorMessage(res));
    break;
  case 'N': /*OTICE*/
  case 'I': /*NFO*/
    /* Infos and notice from Postgre are not the primary purpose of OML.
     * We only display them as debug messages. */
  case 'L': /*OG*/
  case 'D': /*EBUG*/
    logdebug("psql:%s': %s", (char*)arg, PQresultErrorMessage(res));
    break;
  default:
    logwarn("'psql:%s': Unknown notice: %s", (char*)arg, PQresultErrorMessage(res));
  }
}


/**
 * @brief Create a Postgre database
 * @param db the databse to associate with the sqlite3 database
 * @return 0 if successful, -1 otherwise
 */
int
psql_create_database(Database* db)
{
  MString *admin_conninfo = mstring_create ();
  MString *conninfo = mstring_create ();
  MString *str = mstring_create ();
  PGconn     *conn;
  PGresult *res = NULL;

  mstring_sprintf (admin_conninfo, "%s user=%s dbname=postgres", pg_conninfo, pg_user);

  /*
   * Make a connection to the database server -- check if the
   * requested database exists or not by connecting to the 'postgres'
   * database and querying that.
   */
  conn = PQconnectdb(mstring_buf (admin_conninfo));

  /* Check to see that the backend connection was successfully made */
  if (PQstatus(conn) != CONNECTION_OK) {
    logerror("psql: Connection to database server failed: %s\n", PQerrorMessage(conn));
    goto fail_exit;
  }

  PQsetNoticeReceiver(conn, psql_receive_notice, "postgres");

  mstring_sprintf (str, "SELECT datname from pg_database where datname='%s';", db->name);
  res = PQexec (conn, mstring_buf (str));

  if (PQresultStatus (res) != PGRES_TUPLES_OK) {
    logerror ("psql: Could not get list of existing databases\n");
    goto fail_exit;
  }

  /* No result rows means database doesn't exist, so create it instead */
  if (PQntuples (res) == 0) {
    PQclear (res);
    loginfo ("psql:%s: Database does not exist, creating it\n", db->name);
    mstring_set (str, "");
    mstring_sprintf (str, "CREATE DATABASE \"%s\";", db->name);

    res = PQexec (conn, mstring_buf (str));
    if (PQresultStatus (res) != PGRES_COMMAND_OK) {
      logerror ("psql:%s: Could not create database: %s\n", db->name, PQerrorMessage (conn));
      goto fail_exit;
    }
  }

  PQclear (res);
  PQfinish (conn);

  mstring_sprintf (conninfo, "%s user=%s dbname=%s", pg_conninfo, pg_user, db->name);

  /* Make a connection to the database server -- check if the requested database exists or not */
  conn = PQconnectdb(mstring_buf (conninfo));

  /* Check to see that the backend connection was successfully made */
  if (PQstatus(conn) != CONNECTION_OK) {
    logerror("psql:%s: Connection to database server failed: %s", db->name, PQerrorMessage(conn));
    goto fail_exit;
  }
  PQsetNoticeReceiver(conn, psql_receive_notice, db->name);

  PsqlDB* self = (PsqlDB*)xmalloc(sizeof(PsqlDB));
  self->conn = conn;
  self->last_commit = time (NULL);

  db->create = psql_create_database;
  db->release = psql_release;
  db->table_create = psql_table_create;
  db->table_create_meta = psql_table_create_meta;
  db->table_free = psql_table_free;
  db->insert = psql_insert;
  db->add_sender_id = psql_add_sender_id;
  db->get_metadata = psql_get_metadata;
  db->set_metadata = psql_set_metadata;
  db->get_table_list = psql_get_table_list;

  db->handle = self;

  begin_transaction (self);
  return 0;

 fail_exit:
  if (res)
    PQclear (res);
  PQfinish (conn);

  mstring_delete (admin_conninfo);
  mstring_delete (conninfo);
  mstring_delete (str);
  return -1;
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
