#ifndef __VAPP_H__
#define __VAPP_H__

#include "vhost.h"
#include "vcfg.h"

struct vappmain;
struct vappmain_ops {
    int (*need_log_stdout)(struct vappmain*);
    int (*need_daemonize) (struct vappmain*);
    int (*run)            (struct vappmain*, const char*);
};

struct vappmain {
    int  need_stdout;
    int  daemonized;

    struct vconfig cfg;
    struct vhost  host;
    struct vappmain_ops* ops;
};

int  vappmain_init(struct vappmain*);
void vappmain_deinit(struct vappmain*);

#endif

