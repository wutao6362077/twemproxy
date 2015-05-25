/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _NC_SERVER_H_
#define _NC_SERVER_H_

#include <nc_core.h>

/*
 * server_pool is a collection of servers and their continuum. Each
 * server_pool is the owner of a single proxy connection and one or
 * more client connections. server_pool itself is owned by the current
 * context.
 *
 * Each server is the owner of one or more server connections. server
 * itself is owned by the server_pool.
 *
 *  +-------------+
 *  |             |<---------------------+
 *  |             |<------------+        |
 *  |             |     +-------+--+-----+----+--------------+
 *  |   pool 0    |+--->|          |          |              |
 *  |             |     | server 0 | server 1 | ...     ...  |
 *  |             |     |          |          |              |--+
 *  |             |     +----------+----------+--------------+  |
 *  +-------------+                                             //
 *  |             |
 *  |             |
 *  |             |
 *  |   pool 1    |
 *  |             |
 *  |             |
 *  |             |
 *  +-------------+
 *  |             |
 *  |             |
 *  .             .
 *  .    ...      .
 *  .             .
 *  |             |
 *  |             |
 *  +-------------+
 *            |
 *            |
 *            //
 */

typedef uint32_t (*hash_t)(const char *, size_t);

struct continuum {
    uint32_t index;  /* server index */
    uint32_t value;  /* hash value */
};

struct server {
    uint32_t           idx;           /* server index */
    struct server_pool *owner;        /* owner pool */

    struct string      pname;         /* name:port:weight (ref in conf_server) */
    struct string      name;          /* name (ref in conf_server) */
    uint16_t           port;          /* port */
    uint32_t           weight;        /* weight */
    struct sockinfo    info;          /* socket info */

    uint32_t           ns_conn_q;     /* # server connection */
    struct conn_tqh    s_conn_q;      /* server connection q */

    int64_t            next_retry;    /* next retry time in usec */
    uint32_t           failure_count; /* # consecutive failures */
};

struct server_pool {
    TAILQ_ENTRY(server_pool)    pool_tqe;
    uint32_t           idx;                  /* pool index */
    struct context     *ctx;                 /* owner context */

    struct conn        *p_conn;              /* proxy connection (listener) */
    uint32_t           nc_conn_q;            /* # client connection */
    struct conn_tqh    c_conn_q;             /* client connection q */

    struct array       server;               /* server[] */
    uint32_t           ncontinuum;           /* # continuum points */
    uint32_t           nserver_continuum;    /* # servers - live and dead on continuum (const) */
    struct continuum   *continuum;           /* continuum */
    uint32_t           nlive_server;         /* # live server */
    int64_t            next_rebuild;         /* next distribution rebuild time in usec */

    struct string      name;                 /* pool name (ref in conf_pool) */
    struct string      addrstr;              /* pool address (ref in conf_pool) */
    struct string      redis_auth;           /* redis_auth password */
    uint16_t           port;                 /* port */
    struct sockinfo    info;                 /* socket info */
    mode_t             perm;                 /* socket permission */
    int                dist_type;            /* distribution type (dist_type_t) */
    int                key_hash_type;        /* key hash type (hash_type_t) */
    hash_t             key_hash;             /* key hasher */
    struct string      hash_tag;             /* key hash tag (ref in conf_pool) */
    int                timeout;              /* timeout in msec */
    int                backlog;              /* listen backlog */
    int                redis_db;             /* redis database to connect to */
    uint32_t           client_connections;   /* maximum # client connection */
    uint32_t           server_connections;   /* maximum # server connection */
    int64_t            server_retry_timeout; /* server retry timeout in usec */
    uint32_t           server_failure_limit; /* server failure limit */
    unsigned           auto_eject_hosts:1;   /* auto_eject_hosts? */
    unsigned           preconnect:1;         /* preconnect? */
    unsigned           redis:1;              /* redis? */
    enum server_pools_reload_state {
        RSTATE_OLD_AND_ACTIVE,     /* Normal state for an active pool */
        RSTATE_OLD_TO_SHUTDOWN,    /* To shut down for replacement */
        RSTATE_OLD_DRAINING,       /* Shutting down; being replaced */
        RSTATE_NEW_WAIT_FOR_OLD,   /* Waiting for old pool to wrap up */
        RSTATE_NEW,                /* Totally pool, with nothing to wait */
    } reload_state;
    struct server_pool *pool_counterpart;   /* old->new or new->old link */
};

TAILQ_HEAD(server_pools, server_pool);

void server_ref(struct conn *conn, void *owner);
void server_unref(struct conn *conn);
int server_timeout(struct conn *conn);
bool server_active(struct conn *conn);
rstatus_t server_init(struct array *server, struct array *conf_server, struct server_pool *sp);
void server_deinit(struct array *server);
struct conn *server_conn(struct server *server);
rstatus_t server_connect(struct context *ctx, struct server *server, struct conn *conn);
void server_close(struct context *ctx, struct conn *conn);
void server_connected(struct context *ctx, struct conn *conn);
void server_ok(struct context *ctx, struct conn *conn);

struct server_pool *server_pool_new(void);
void server_pool_free(struct server_pool *);

typedef rstatus_t (*pool_each_t)(struct server_pool *, void *);
rstatus_t server_pool_each(struct server_pools *server_pools, pool_each_t func, void *key);

uint32_t server_pools_n(struct server_pools *server_pools);  /* number of pools in the pools list */
uint32_t server_pool_idx(struct server_pool *pool, uint8_t *key, uint32_t keylen);
struct conn *server_pool_conn(struct context *ctx, struct server_pool *pool, uint8_t *key, uint32_t keylen);
rstatus_t server_pool_run(struct server_pool *pool);
rstatus_t server_pool_preconnect(struct context *ctx);
void server_pool_disconnect(struct server_pools *server_pools);
rstatus_t server_pools_init(struct server_pools *server_pools, struct array *conf_pool, struct context *ctx);
void server_pools_deinit(struct server_pools *server_pools);

/* Initiate the pool replacement process. */
rstatus_t server_pools_kick_replacement(struct server_pools *old, struct server_pools *new);
/* Attempt to complete the replacement and return true if reload succeeded. */
bool server_pools_finish_replacement(struct server_pools *old);

/*
 * A helper function to traverse the whole tree of pools/servers/connections.
 */
enum nc_morph_elem_type {
    NC_ELEMENT_IS_POOL,
    NC_ELEMENT_IS_SERVER,
    NC_ELEMENT_IS_CONNECTION,
};
typedef void *(*nc_morphism_f)(enum nc_morph_elem_type, void *elem, void *acc);
void *server_pools_fold(struct server_pools *server_pools, nc_morphism_f f, void *acc0);

void server_pools_log(int level, const char *prefix, struct server_pools *);

#endif
