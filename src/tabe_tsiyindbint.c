/*
 * Copyright 1999, TaBE Project, All Rights Reserved.
 * Copyright 1999, Pai-Hsiang Hsiao, All Rights Reserved.
 *
 * $Id: tabe_tsiyindbint.c,v 1.1 2000/12/09 09:14:12 thhsieh Exp $
 *
 */
#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <db.h>
#ifdef HPUX
#  define _XOPEN_SOURCE_EXTENDED
#  define _INCLUDE_XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
#else
#  include <netinet/in.h>
#endif

#include "tabe.h"

static void tabeTsiYinDBClose(struct TsiYinDB *tsiyindb);
static int  tabeTsiYinDBRecordNumber(struct TsiYinDB *tsiyindb);
static int  tabeTsiYinDBStoreTsiYin(struct TsiYinDB *tsiyindb,
				    struct TsiYinInfo *tsiyin);
static int  tabeTsiYinDBLookupTsiYin(struct TsiYinDB *tsiyindb,
				     struct TsiYinInfo *tsiyin);
static int  tabeTsiYinDBCursorSet(struct TsiYinDB *tsiyindb,
				  struct TsiYinInfo *tsiyin);
static int  tabeTsiYinDBCursorNext(struct TsiYinDB *tsiyindb,
				   struct TsiYinInfo *tsiyin);
static int  tabeTsiYinDBCursorPrev(struct TsiYinDB *tsiyindb,
				   struct TsiYinInfo *tsiyin);

struct TsiYinDBDataDB {
  unsigned long int  yinlen;
  unsigned long int  tsinum;
};

static int  TsiYinDBStoreTsiYinDB(struct TsiYinDB *tsiyindb,
				  struct TsiYinInfo *tsiyin);
static int  TsiYinDBLookupTsiYinDB(struct TsiYinDB *tsiyindb,
				   struct TsiYinInfo *tsiyin);
static void TsiYinDBPackDataDB(struct TsiYinInfo *tsiyin, DBT *dat);
static void TsiYinDBUnpackDataDB(struct TsiYinInfo *tsiyin, DBT *dat);

/*
 * open a TsiYinDB with the given type and name
 *
 * return pointer to TsiYinDB if success, NULL if failed
 *
 */
struct TsiYinDB *
tabeTsiYinDBOpen(int type, const char *db_name, int flags)
{
  struct TsiYinDB *tsiyindb;
  DB *dbp;

  switch(type) {
  case DB_TYPE_DB:
    tsiyindb = (struct TsiYinDB *)malloc(sizeof(struct TsiYinDB));
    if (!tsiyindb) {
      perror("tabeTsiYinDBOpen()");
      return(NULL);
    }
    memset(tsiyindb, 0, sizeof(struct TsiYinDB));
    tsiyindb->type = type;
    tsiyindb->flags = flags;

    tsiyindb->Close = tabeTsiYinDBClose;
    tsiyindb->RecordNumber = tabeTsiYinDBRecordNumber;
    tsiyindb->Put = tabeTsiYinDBStoreTsiYin;
    tsiyindb->Get = tabeTsiYinDBLookupTsiYin;
    tsiyindb->CursorSet = tabeTsiYinDBCursorSet;
    tsiyindb->CursorNext = tabeTsiYinDBCursorNext;
    tsiyindb->CursorPrev = tabeTsiYinDBCursorPrev;

    if (tsiyindb->flags & DB_FLAG_CREATEDB) {
      if (tsiyindb->flags & DB_FLAG_READONLY) {
        return(NULL);
      }
      else {
	errno = db_open(db_name, DB_BTREE, DB_CREATE,
			0644, NULL, NULL, &dbp);

      }
    }
    else {
      if (tsiyindb->flags & DB_FLAG_READONLY) {
	errno = db_open(db_name, DB_BTREE, DB_RDONLY,
			0444, NULL, NULL, &dbp);
      }
      else {
	errno = db_open(db_name, DB_BTREE, 0        ,
			0644, NULL, NULL, &dbp);

      }
    }
    if (errno > 0) {
      fprintf(stderr, "tabeTsiYinDBOpen(): Can not open DB file %s (%s).\n",
	     db_name, strerror(errno));
      free(tsiyindb);
      return(NULL);
    }
    if (errno < 0) {
      /* DB specific errno */
      fprintf(stderr, "tabeTsiYinDBOpen(): DB error opening DB File %s.\n",
	      db_name);
      free(tsiyindb);
      return(NULL);
    }
    tsiyindb->db_name = (char *)strdup(db_name);
    tsiyindb->dbp = (void *)dbp;
    return(tsiyindb);
  default:
    fprintf(stderr, "tabeTsiYinDBOpen(): Unknown DB type.\n");
    break;
  }

  return(NULL);
}

/*
 * close and flush DB file
 */
static void
tabeTsiYinDBClose(struct TsiYinDB *tsiyindb)
{
  DB  *dbp;
  DBC *dbcp;

  switch(tsiyindb->type) {
  case DB_TYPE_DB:
    dbp = (DB *)tsiyindb->dbp;
    dbcp = (DBC *)tsiyindb->dbcp;
    if (dbcp) {
      dbcp->c_close(dbcp);
      dbcp = (void *)NULL;
    }
    if (dbp) {
      dbp->close(dbp, 0);
      dbp = (void *)NULL;
    }
    return;
  default:
    fprintf(stderr, "tabeTsiYinDBClose(): Unknown DB type.\n");
    break;
  }
  return;
}

/*
 * returns the number of record in TsiYin DB
 */
static int
tabeTsiYinDBRecordNumber(struct TsiYinDB *tsiyindb)
{
  DB *dbp;
  DB_BTREE_STAT *sp;

  switch(tsiyindb->type) {
  case DB_TYPE_DB:
    dbp = (DB *)tsiyindb->dbp;
    errno = dbp->stat(dbp, &sp, NULL, 0);
    if (!errno) {
      return(sp->bt_nrecs);
    }
    break;
  default:
    fprintf(stderr, "tabeTsiYinDBRecordNumber(): Unknown DB type.\n");
    break;
  }
  return(0);
}

/*
 * store TsiYin in designated DB
 *
 * return 0 if success, -1 if failed
 *
 */
static int
tabeTsiYinDBStoreTsiYin(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  int rval;

  if (tsiyindb->flags & DB_FLAG_READONLY) {
    fprintf(stderr, "tabeTsiDBStoreTsi(): writing a read-only DB.\n");
    return(-1);
  }

  switch(tsiyindb->type) {
  case DB_TYPE_DB:
    rval = TsiYinDBStoreTsiYinDB(tsiyindb, tsiyin);
    return(rval);
  default:
    fprintf(stderr, "tabeTsiYinDBStoreTsiYin(): Unknown DB type.\n");
    break;
  }

  return(-1);
}

/*
 * lookup TsiYin in designated DB
 *
 * return 0 if success, -1 if failed
 *
 */
static int
tabeTsiYinDBLookupTsiYin(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  int rval;

  switch(tsiyindb->type) {
  case DB_TYPE_DB:
    rval = TsiYinDBLookupTsiYinDB(tsiyindb, tsiyin);
    return(rval);
  default:
    fprintf(stderr, "tabeTsiYinDBLookupTsiYin(): Unknown DB type.\n");
    break;
  }

  return(-1);
}

static void
TsiYinDBPackDataDB(struct TsiYinInfo *tsiyin, DBT *dat)
{
  struct TsiYinDBDataDB *d;
  int datalen, tsilen;
  unsigned char *data;

  tsilen = tsiyin->yinlen * tsiyin->tsinum * 2;
  datalen = sizeof(struct TsiYinDBDataDB) + sizeof(unsigned char)*tsilen;
  data = (unsigned char *)malloc(sizeof(unsigned char)*datalen);
  memset(data, 0, sizeof(unsigned char)*datalen);
  d = (struct TsiYinDBDataDB *)data;

  /* convert to network byte order */
  d->yinlen = htonl(tsiyin->yinlen);
  d->tsinum = htonl(tsiyin->tsinum);
  memcpy(data+sizeof(struct TsiYinDBDataDB), tsiyin->tsidata,
         sizeof(unsigned char)*tsilen);

  dat->data = data;
  dat->size = datalen;
}

static void
TsiYinDBUnpackDataDB(struct TsiYinInfo *tsiyin, DBT *dat)
{
  int tsilen;
  struct TsiYinDBDataDB d;

  memset(&d, 0, sizeof(struct TsiYinDBDataDB));
  memcpy(&d, dat->data, sizeof(struct TsiYinDBDataDB));
  /* convert to system byte order */
  tsiyin->yinlen = ntohl(d.yinlen);
  tsiyin->tsinum = ntohl(d.tsinum);
  tsilen         = tsiyin->yinlen * tsiyin->tsinum * 2;

  if (tsiyin->tsidata) {
    free(tsiyin->tsidata);
    tsiyin->tsidata = (ZhiStr)NULL;
  }

  if (tsilen) {
    struct TsiYinDBDataDB *stmp;
    stmp = (struct TsiYinDBDataDB *)dat->data + 1;
    tsiyin->tsidata = (ZhiStr)malloc(sizeof(unsigned char)*tsilen);
    memcpy((void *)tsiyin->tsidata, (void *)stmp, sizeof(unsigned char)*tsilen);
  }
}

static int
TsiYinDBStoreTsiYinDB(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  DBT key, dat;
  DB *dbp;

  memset(&key, 0, sizeof(key));
  memset(&dat, 0, sizeof(dat));

  key.data = tsiyin->yin;
  key.size = sizeof(Yin)*tsiyin->yinlen;

  TsiYinDBPackDataDB(tsiyin, &dat);

  dbp = tsiyindb->dbp;
  if (tsiyindb->flags & DB_FLAG_OVERWRITE) {
    errno = dbp->put(dbp, NULL, &key, &dat, 0);
  }
  else {
    errno = dbp->put(dbp, NULL, &key, &dat, DB_NOOVERWRITE);
  }
  if (errno > 0) {
    fprintf(stderr, "TsiYinDBStoreTsiYinDB(): can not store DB. (%s)\n",
	    strerror(errno));
    return(-1);
  }
  if (errno < 0) {
    switch(errno) {
    case DB_KEYEXIST:
#ifdef MYDEBUG
      fprintf(stderr, "TsiYinDBStoreTsiYinDB(): tsiyin exist.\n");
#endif
      return(-1);
    default:
      fprintf(stderr, "TsiYinDBStoreTsiYinDB(): unknown DB error.\n");
      return(-1);
    }
  }

  free(dat.data);
  return(0);
}

static int
TsiYinDBLookupTsiYinDB(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  DBT key, dat;
  DB *dbp;

  memset(&key, 0, sizeof(key));
  memset(&dat, 0, sizeof(dat));

  key.data = tsiyin->yin;
  key.size = sizeof(Yin)*tsiyin->yinlen;

  dbp = tsiyindb->dbp;
  errno = dbp->get(dbp, NULL, &key, &dat, 0);
  if (errno > 0) {
    fprintf(stderr, "TsiYinDBLookupTsiYinDB(): can not lookup DB. (%s)\n",
	    strerror(errno));
    return(-1);
  }
  if (errno < 0) {
    switch(errno) {
    case DB_NOTFOUND:
#ifdef MYDEBUG
//      fprintf(stderr, "TsiYinDBLookupTsiYinDB(): tsiyin does not exist.\n");
#endif
      return(-1);
    default:
      fprintf(stderr, "TsiYinDBLookupTsiYinDB(): unknown DB error.\n");
      return(-1);
    }
  }

  TsiYinDBUnpackDataDB(tsiyin, &dat);

  return(0);
}

static int
tabeTsiYinDBCursorSet(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  DB  *dbp;
  DBC *dbcp;
  DBT  key, dat;

  dbp  = tsiyindb->dbp;
  dbcp = tsiyindb->dbcp;
  if (dbcp) {
    dbcp->c_close(dbcp);
  }

#if DB_VERSION_MINOR > 6 || (DB_VERSION_MINOR == 6 && DB_VERSION_PATCH > 4)
  dbp->cursor(dbp, NULL, &dbcp, 0);
#else
  dbp->cursor(dbp, NULL, &dbcp);
#endif
  tsiyindb->dbcp = dbcp;

  memset(&key, 0, sizeof(key));
  memset(&dat, 0, sizeof(dat));

  if (tsiyin->yinlen) {
    key.data = tsiyin->yin;
    key.size = tsiyin->yinlen;
    errno = dbcp->c_get(dbcp, &key, &dat, DB_SET);
  }
  else {
    errno = dbcp->c_get(dbcp, &key, &dat, DB_FIRST);
  }
  if (errno > 0) {
    fprintf(stderr, "tabeTsiYinDBCursorSet(): error setting cursor. (%s)\n",
	    strerror(errno));
    return(-1);
  }
  if (errno < 0) {
    switch(errno) {
    default:
      fprintf(stderr, "tabeTsiYinDBCursorSet(): Unknown error.\n");
      return(-1);
    }
  }

  if (tsiyin->yin) {
    free(tsiyin->yin);
    tsiyin->yin = (Yin *)NULL;
  }
  tsiyin->yin = (Yin *)malloc(sizeof(Yin)*tsiyin->yinlen);
  memcpy(tsiyin->yin, key.data, sizeof(Yin)*tsiyin->yinlen);

  TsiYinDBUnpackDataDB(tsiyin, &dat);  

  return(0);
}

static int
tabeTsiYinDBCursorNext(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  DB  *dbp;
  DBC *dbcp;
  DBT  key, dat;

  dbp  = tsiyindb->dbp;
  dbcp = tsiyindb->dbcp;
  if (!dbcp) {
    return(-1);
  }

  memset(&key, 0, sizeof(key));
  memset(&dat, 0, sizeof(dat));

  errno = dbcp->c_get(dbcp, &key, &dat, DB_NEXT);
  if (errno < 0) {
    switch(errno) {
    case DB_NOTFOUND:
      return(-1);
    default:
      return(-1);
    }
  }

  if (tsiyin->yin) {
    free(tsiyin->yin);
    tsiyin->yin = (Yin *)NULL;
  }
  tsiyin->yin = (Yin *)malloc(sizeof(Yin)*tsiyin->yinlen);
  memcpy(tsiyin->yin, key.data, sizeof(Yin)*tsiyin->yinlen);

  TsiYinDBUnpackDataDB(tsiyin, &dat);

  return(0);
}

static int
tabeTsiYinDBCursorPrev(struct TsiYinDB *tsiyindb, struct TsiYinInfo *tsiyin)
{
  DB  *dbp;
  DBC *dbcp;
  DBT  key, dat;

  dbp  = tsiyindb->dbp;
  dbcp = tsiyindb->dbcp;
  if (!dbcp) {
    return(-1);
  }

  memset(&key, 0, sizeof(key));
  memset(&dat, 0, sizeof(dat));

  errno = dbcp->c_get(dbcp, &key, &dat, DB_PREV);
  if (errno < 0) {
    switch(errno) {
    case DB_NOTFOUND:
      return(-1);
    default:
      return(-1);
    }
  }

  if (tsiyin->yin) {
    free(tsiyin->yin);
    tsiyin->yin = (Yin *)NULL;
  }
  tsiyin->yin = (Yin *)malloc(sizeof(Yin)*tsiyin->yinlen);
  memcpy(tsiyin->yin, key.data, sizeof(Yin)*tsiyin->yinlen);

  TsiYinDBUnpackDataDB(tsiyin, &dat);

  return(0);
}
