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
    int   (*exit)       (struct vhost*);
    int   (*run)        (struct vhost*);
    int   (*stabilize)  (struct vhost*);
    void  (*dump)       (struct vhost*);
    char* (*version)    (struct vhost*);
    int   (*bogus_query)(struct vhost*, int, struct sockaddr_in*);
};

struct vhost_srvc_ops {
    int (*post)  (struct vhost*, vsrvcHash*, struct sockaddr_in*);
    int (*unpost)(struct vhost*, vsrvcHash*, struct sockaddr_in*);
    int (*probe) (struct vhost*, vsrvcHash*, vsrvcInfo_iterate_addr_t, void*);
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
    struct vhost_srvc_ops* srvc_ops;
};

const char* vhost_get_version(void);
int  vhost_init  (struct vhost*, struct vconfig*);
void vhost_deinit(struct vhost*);

#endif

