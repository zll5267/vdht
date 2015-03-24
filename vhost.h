#ifndef __VHOST_H__
#define __VHOST_H__

#include "vrpc.h"
#include "vnode.h"
#include "vlsctl.h"
#include "vmsger.h"
#include "vticker.h"

struct vhost;
struct vhost_ops {
    int   (*start)      (struct vhost*);
    int   (*stop)       (struct vhost*);
    int   (*join)       (struct vhost*, struct sockaddr_in*);
    int   (*stabilize)  (struct vhost*);
    int   (*daemonize)  (struct vhost*);
    int   (*shutdown)   (struct vhost*);
    void  (*dump)       (struct vhost*);
    char* (*version)    (struct vhost*);
    int   (*bogus_query)(struct vhost*, int, struct sockaddr_in*);
};

struct vhost_svc_ops {
    int (*post)  (struct vhost*, vsrvcId*, struct sockaddr_in*);
    int (*unpost)(struct vhost*, vsrvcId*, struct sockaddr_in*);
    int (*probe) (struct vhost*, vsrvcId*, vsrvcInfo_iterate_addr_t, void*);
};

struct vhost {
    int  to_quit;
    int  tick_tmo;
    vnodeId myid;
    struct sockaddr_in zaddr;

    struct vthread  thread;
    struct vmsger   msger;
    struct vrpc     rpc;
    struct vwaiter  waiter;
    struct vticker  ticker;
    struct vroute   route;
    struct vnode    node;
    struct vlsctl   lsctl;

    struct vconfig*   cfg;
    struct vhost_ops* ops;
    struct vhost_svc_ops* svc_ops;
};

const char* vhost_get_version(void);
struct vhost* vhost_create(struct vconfig*);
void vhost_destroy(struct vhost*);

#endif
