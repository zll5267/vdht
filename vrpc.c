#include "vglobal.h"
#include "vrpc.h"

struct vunix_domain {
    struct sockaddr_un addr;
    int sock_fd;
};

/*
 * to open a udp socket for unix rpc.
 * @addr:
 */
static
void* _vrpc_unix_open(struct vsockaddr* addr)
{
    struct sockaddr_un* saddr = to_sockaddr_sun(addr);
    struct vunix_domain* unx = NULL;
    int ret = 0;
    int fd  = 0;
    vassert(addr);

    unx = (struct vunix_domain*)malloc(sizeof(*unx));
    vlogEv((!unx), elog_malloc);
    retE_p((!unx));
    memset(unx, 0, sizeof(*unx));
    memcpy(&unx->addr, saddr, sizeof(*saddr));

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    vlogEv((fd < 0), elog_socket);
    ret1E_p((fd < 0), free(unx));

    unlink(saddr->sun_path);
    ret = bind(fd, (struct sockaddr*)saddr, sizeof(*saddr));
    vlogEv((ret < 0), elog_bind);
    if (ret < 0) {
        free(unx);
        close(fd);
        retE_p((1));
    }

    unx->sock_fd = fd;
    return unx;
}

/*
 *  to send a msg for rpc.
 *
 * @impl: pointer to struct vudp; handler of udp rpc.
 * @msg :
 */
static
int _vrpc_unix_sndto(void* impl, struct vmsg_sys* msg)
{
    struct vunix_domain* unx = (struct vunix_domain*)impl;
    int ret = 0;

    vassert(unx);
    vassert(msg);

    ret = sendto(unx->sock_fd,
                 msg->data,
                 msg->len,
                 0,
                (struct sockaddr*)&msg->addr.vsun_addr,
                sizeof(struct sockaddr_un));
    vlogEv((ret < 0), elog_sendto);
    retE((ret < 0));
    return ret;
}

/* to receive a msg, including from where.
 *
 * @impl:
 * @msg:
 */
static
int _vrpc_unix_rcvfrom(void* impl, struct vmsg_sys* msg)
{
    struct vunix_domain* unx = (struct vunix_domain*)impl;
    int len = sizeof(struct sockaddr_un);
    int ret = 0;

    vassert(unx);
    vassert(msg);

    ret = recvfrom(unx->sock_fd,
                msg->data,
                msg->len,
                0,
               (struct sockaddr*)&msg->addr.vsun_addr,
               (socklen_t*)&len);
    vlogEv((ret < 0), elog_recvfrom);
    retE((ret < 0));
    msg->len = ret;

    return ret;
}

static
void _vrpc_unix_close(void* impl)
{
    struct vunix_domain *unx = (struct vunix_domain*)impl;
    vassert(unx);

     if (unx->sock_fd > 0) {
        close(unx->sock_fd);
    }
    free(unx);
    return ;
}

/*
 * to close udp socket.
 * @impl:
 */
static
int _vrpc_unix_getfd(void* impl)
{
    struct vunix_domain* unx = (struct vunix_domain*)impl;
    vassert(unx);

    return unx->sock_fd;
}


/*
 * to dump
 * @impl
 */
static
void _vrpc_unix_dump(void* impl)
{
    struct vunix_domain* unx = (struct vunix_domain*)impl;
    vassert(unx);

    printf("unix domain ");
    printf("address: %s ", unx->addr.sun_path);
    printf("fd: %d", unx->sock_fd);
    return ;
}

/*
 * downword method set for udp mode.
 */
static
struct vrpc_base_ops unix_base_ops = {
    .open    = _vrpc_unix_open,
    .sndto   = _vrpc_unix_sndto,
    .rcvfrom = _vrpc_unix_rcvfrom,
    .close   = _vrpc_unix_close,
    .getfd   = _vrpc_unix_getfd,
    .dump    = _vrpc_unix_dump
};


/*
 *  for udp rpc
 */
struct vudp {
    struct sockaddr_in addr;
    uint16_t port;
    uint16_t pad;
    int sock_fd;
    int ttl;
};

/*
 * to open a udp socket for rpc.
 * @addr:
 */
static
void* _vrpc_udp_open(struct vsockaddr* addr)
{
    struct sockaddr_in* saddr = to_sockaddr_sin(addr);
    struct vudp* udp = NULL;
    int flags = 0;
    int fd  = 0;
    int ttl = 64;
    int on  = 1;
    int ret = 0;
    vassert(addr);

    udp = (struct vudp*)malloc(sizeof(*udp));
    vlogEv((!udp), elog_malloc);
    retE_p((!udp));
    memset(udp, 0, sizeof(*udp));
    memcpy(&udp->addr, saddr, sizeof(*saddr));

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    vlogEv((fd < 0), elog_socket);
    ret1E_p((fd < 0), free(udp));

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on));
    setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(int));

    errno = 0;
    ret = bind(fd, (struct sockaddr*)saddr, sizeof(*saddr));
    vlogEv((ret < 0), elog_bind);
    if (ret < 0) {
        close(fd);
        free(udp);
        retE_p((1));
    }

    flags = fcntl(fd, F_GETFL, 0);
    ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    vlogEv((ret < 0), elog_fcntl);
    if (ret < 0) {
        close(fd);
        free(udp);
        retE_p((1));
    }

    udp->port = saddr->sin_port;
    udp->sock_fd   = fd;
    return udp;
}

/*
 *  to send a msg for rpc.
 *
 * @impl: pointer to struct vudp; handler of udp rpc.
 * @msg :
 */
static
int _vrpc_udp_sndto(void* impl, struct vmsg_sys* msg)
{
    struct vudp* udp = (struct vudp*)impl;
    struct sockaddr_in* spec_addr = to_sockaddr_sin(&msg->spec);
    struct in_pktinfo* pi = NULL;
    struct cmsghdr*  cmsg = NULL;
    char msg_control[BUF_SZ];
    struct iovec iovec[1];
    struct msghdr mhdr;
    int ret = 0;

    vassert(udp);
    vassert(msg);

    memset(msg_control, 0, BUF_SZ);
    memset(iovec, 0, sizeof(iovec));
    memset(&mhdr, 0, sizeof(mhdr));

    iovec[0].iov_base = msg->data;
    iovec[0].iov_len  = msg->len;
    mhdr.msg_name     = to_sockaddr_sin(&msg->addr);
    mhdr.msg_namelen  = sizeof(struct sockaddr_in);
    mhdr.msg_iov      = iovec;
    mhdr.msg_iovlen   = sizeof(iovec)/sizeof(struct iovec);
    mhdr.msg_control  = msg_control;
    mhdr.msg_controllen = BUF_SZ;
    mhdr.msg_flags    = 0;

    cmsg = CMSG_FIRSTHDR(&mhdr);
    cmsg->cmsg_level = IPPROTO_IP;
    cmsg->cmsg_type  = IP_PKTINFO;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(*pi));
    pi = (struct in_pktinfo*)CMSG_DATA(cmsg);
    pi->ipi_spec_dst = (struct in_addr)spec_addr->sin_addr;
    mhdr.msg_controllen = CMSG_SPACE(sizeof(*pi));

    ret = sendmsg(udp->sock_fd, &mhdr, 0);
    vlogEv((ret < 0), elog_sendmsg);
    //vlogEv((ret < 0), vsockaddr_dump(to_sockaddr_sin(&msg->addr)));
    //vlogEv((ret < 0), vsockaddr_dump(to_sockaddr_sin(&msg->spec)));
    if (ret < 0) {
        retE((1));
    }
    return ret;
}

/* to receive a msg, including from where.
 *
 * @impl:
 * @msg:
 */
static
int _vrpc_udp_rcvfrom(void* impl, struct vmsg_sys* msg)
{
    struct vudp* udp = (struct vudp*)impl;
    struct sockaddr_in* spec_addr = to_sockaddr_sin(&msg->spec);
    struct cmsghdr* cmsg = NULL;
    char msg_control[BUF_SZ];
    struct iovec iovec[1];
    struct msghdr mhdr;
    int ret = 0;

    vassert(udp);
    vassert(msg);

    memset(msg_control, 0, BUF_SZ);
    memset(iovec, 0, sizeof(iovec));

    iovec[0].iov_base = msg->data;
    iovec[0].iov_len  = msg->len;
    mhdr.msg_name     = to_sockaddr_sin(&msg->addr);
    mhdr.msg_namelen  = sizeof(struct sockaddr_in);
    mhdr.msg_iov      = iovec;
    mhdr.msg_iovlen   = sizeof(iovec)/sizeof(*iovec);
    mhdr.msg_control  = msg_control;
    mhdr.msg_controllen = BUF_SZ;
    mhdr.msg_flags    = 0;

    ret = recvmsg(udp->sock_fd, &mhdr, 0);
    vlogEv((ret < 0), elog_recvmsg);
    //vlogEv((ret < 0), vsockaddr_dump(to_sockaddr_sin(&msg->addr)));
    //vlogEv((ret < 0), vsockaddr_dump(to_sockaddr_sin(&msg->spec)));
    retE((ret < 0));

    for (cmsg = CMSG_FIRSTHDR(&mhdr); cmsg != NULL; cmsg = CMSG_NXTHDR(&mhdr, cmsg)) {
        struct in_pktinfo* pi = NULL;

        if ((cmsg->cmsg_level != IPPROTO_IP)
            || (cmsg->cmsg_type != IP_PKTINFO)) {
            continue;
        }
        pi = (struct in_pktinfo*)CMSG_DATA(cmsg);
        spec_addr->sin_family = AF_INET;
        spec_addr->sin_port   = udp->port;
        spec_addr->sin_addr   = pi->ipi_spec_dst;
    }
    msg->len = ret;
    return ret;
}

/*
 * to close udp socket.
 * @impl:
 */
static
void _vrpc_udp_close(void* impl)
{
    struct vudp* udp = (struct vudp*)impl;
    vassert(udp);

    if (udp->sock_fd > 0) {
        close(udp->sock_fd);
    }
    free(udp);
    return ;
}

/*
 * to get socket fd.
 * @impl:
 */
static
int _vrpc_udp_getfd(void* impl)
{
    struct vudp* udp = (struct vudp*)impl;
    vassert(udp);
    return udp->sock_fd;
}

/*
 * to dump
 * @impl
 */
static
void _vrpc_udp_dump(void* impl)
{
    struct vudp* udp = (struct vudp*)impl;
    char buf[64];
    int  port = 0;
    vassert(udp);

    vsockaddr_unconvert(&udp->addr, buf, 64, (uint16_t*)&port);
    printf("udp,");
    printf("address: %s:%d,", buf, port);
    printf("fd:%d ", udp->sock_fd);
    return ;
}

/*
 * downword method set for udp mode.
 */
static
struct vrpc_base_ops udp_base_ops = {
    .open    = _vrpc_udp_open,
    .sndto   = _vrpc_udp_sndto,
    .rcvfrom = _vrpc_udp_rcvfrom,
    .close   = _vrpc_udp_close,
    .getfd   = _vrpc_udp_getfd,
    .dump    = _vrpc_udp_dump
};

/*
 *
 */
static
struct vrpc_base_ops* rpc_base_ops[VRPC_MODE_BUTT] = {
    &unix_base_ops,
    &udp_base_ops,
    NULL
};

/*
 * for rpc upword methods
 */
static
int _vrpc_snd(struct vrpc* rpc)
{
    int ret = 0;
    vassert(rpc);
    vassert(rpc->impl);
    vassert(rpc->msger);

    if (rpc->sndm) {
        // previous sys msg must have been sending over.
        vmsg_sys_free(rpc->sndm);
        rpc->sndm = NULL;
    }

    ret = rpc->msger->ops->pop(rpc->msger, &rpc->sndm);
    retE((ret < 0));

    ret = rpc->base_ops->sndto(rpc->impl, rpc->sndm);
    if (ret < 0) {
        rpc->stat.nerrs++;
        retE((1));
    }
    rpc->stat.snd_bytes += ret;
    rpc->stat.nsnds++;
    return 0;
}

static
int _vrpc_rcv(struct vrpc* rpc)
{
    int ret = 0;

    vassert(rpc);
    vassert(rpc->impl);
    vassert(rpc->msger);

    vmsg_sys_refresh(rpc->rcvm, 8*BUF_SZ); //refresh the receving buf.
    ret = rpc->base_ops->rcvfrom(rpc->impl, rpc->rcvm);
    if (ret < 0) {
        rpc->stat.nerrs++;
        retE((1));
    }
    rpc->stat.rcv_bytes += ret;
    rpc->stat.nrcvs++;

    ret = rpc->msger->ops->dsptch(rpc->msger, rpc->rcvm);
    retE((ret < 0));

    return 0;
}

static
int _vrpc_err(struct vrpc* rpc)
{
    vassert(rpc);
    vassert(rpc->impl);

    rpc->base_ops->close(rpc->impl);
    rpc->impl = rpc->base_ops->open(&rpc->addr);
    retE((!rpc->impl));

    return 0;
}

static
int _vrpc_getId(struct vrpc* rpc)
{
    vassert(rpc);
    vassert(rpc->impl);

    return rpc->base_ops->getfd(rpc->impl);
}

static
void _vrpc_stat(struct vrpc* rpc, struct vrpc_stat* buf)
{
    vassert(rpc);
    vassert(buf);

    memcpy(buf, &rpc->stat, sizeof(*buf));
    return ;
}

static
void _vrpc_dump(struct vrpc* rpc)
{
    vassert(rpc);
    printf("{ ");
    printf("nerrs:%d, ", rpc->stat.nerrs);
    printf("nsnds:%d, ", rpc->stat.nsnds);
    printf("nrcvs:%d, ", rpc->stat.nrcvs);
    printf("send bytes:%d,", rpc->stat.snd_bytes);
    printf("rcved bytes:%d,", rpc->stat.rcv_bytes);
    rpc->base_ops->dump(rpc->impl);
    printf(" }\n");
    return ;
}

/*
 * generic rpc method set with user torwareds. it encapusulated socket
 * details from user.
 */
static
struct vrpc_ops rpc_ops = {
    .snd   = _vrpc_snd,
    .rcv   = _vrpc_rcv,
    .err   = _vrpc_err,
    .getId = _vrpc_getId,
    .stat  = _vrpc_stat,
    .dump  = _vrpc_dump
};

/*
 * each rpc channel must has one msger, and only one msger to deal with
 * upwords user.
 * user only need to be aware of msg format.DO NOT need details of rpc.
 *
 * @rpc:  [in, out]
 * @msger:[in];
 * @mode :[in];
 * @addr :[in]
 */
int vrpc_init(struct vrpc* rpc, struct vmsger* msger, int mode, struct vsockaddr* addr)
{
    struct vmsg_sys* sm = NULL;
    vassert(rpc);
    vassert(addr);
    vassert(vrpc_mode_ok(mode));

    memset(rpc, 0, sizeof(*rpc));
    memcpy(&rpc->addr, addr, sizeof(*addr));
    rpc->mode = mode;
    rpc->msger= msger;
    rpc->impl = NULL;
    rpc->ops  = &rpc_ops;
    rpc->base_ops = rpc_base_ops[mode];

    sm = vmsg_sys_alloc(8*BUF_SZ);
    vlogEv((!sm), elog_vmsg_sys_alloc);
    retE((!sm));
    vlist_init(&sm->list);

    rpc->rcvm = sm;
    rpc->sndm = NULL;

    rpc->stat.nerrs = 0;
    rpc->stat.nsnds = 0;
    rpc->stat.nrcvs = 0;
    rpc->stat.snd_bytes = 0;
    rpc->stat.rcv_bytes = 0;

    rpc->impl = rpc->base_ops->open(addr);
    ret1E((!rpc->impl), vmsg_sys_free(sm));
    return 0;
}

/*
 * @rpc:
 */
void vrpc_deinit(struct vrpc* rpc)
{
    vassert(rpc);
    vassert(rpc->impl);

    if (rpc->sndm) {
        vmsg_sys_free(rpc->sndm);
        rpc->sndm = NULL;
    }
    if (rpc->rcvm) {
        vmsg_sys_free(rpc->rcvm);
        rpc->sndm = NULL;
    }
    rpc->base_ops->close(rpc->impl);
    return ;
}

/*
 * @wt:
 * @rpc:
 */

static
int _vwaiter_add(struct vwaiter* wt, struct vrpc* rpc)
{
    vassert(wt);
    vassert(rpc);

    vlock_enter(&wt->lock);
    varray_add_tail(&wt->rpcs, rpc);
    wt->reset = 1;
    vlock_leave(&wt->lock);
    return 0;
}

/*
 * @wt:
 * @rpc:
 */
static
int _vwaiter_remove(struct vwaiter* wt, struct vrpc* rpc)
{
    int i  = 0;
    vassert(wt);
    vassert(rpc);

    vlock_enter(&wt->lock);
    for (; i < varray_size(&wt->rpcs); i++) {
        if (varray_get(&wt->rpcs, i) == (void*)rpc) {
            break;
        }
    }
    if (i < varray_size(&wt->rpcs)) {
        varray_del(&wt->rpcs, i);
        wt->reset = 1;
    }
    vlock_leave(&wt->lock);
    return 0;
}

static
int _aux_fdsets_cb(void* item, void* cookie)
{
    struct vwaiter* wt = (struct vwaiter*)cookie;
    struct vrpc* rpc = (struct vrpc*)item;
    int fd = 0;

    vassert(wt);
    vassert(rpc);

    fd = rpc->ops->getId(rpc);
    wt->maxfd = (fd >= wt->maxfd) ? fd + 1: wt->maxfd;

    FD_SET(fd, &wt->rfds);
    FD_SET(fd, &wt->efds);
    if (rpc->msger->ops->popable(rpc->msger)) {
        FD_SET(fd, &wt->wfds);
    }
    return 0;
}

static
int _aux_laundry_cb(void* item, void* cookie)
{
    struct vwaiter* wt = (struct vwaiter*)cookie;
    struct vrpc* rpc = (struct vrpc*)item;
    int fd = 0;

    vassert(wt);
    vassert(rpc);

    fd = rpc->ops->getId(rpc);
    if (FD_ISSET(fd, &wt->rfds)){
        rpc->ops->rcv(rpc);
    }
    if (FD_ISSET(fd, &wt->wfds)) {
        rpc->ops->snd(rpc);
    }
    if (FD_ISSET(fd, &wt->efds)) {
        rpc->ops->err(rpc);
    }
    return 0;
}

/*
 * @wt:
 * @tgt:[out]: which rpc
 * @rwe [out]: type of status change of socket descriptor
 */
static
int _vwaiter_laundry(struct vwaiter* wt)
{
    vassert(wt);

    vlock_enter(&wt->lock);
    FD_ZERO(&wt->rfds);
    FD_ZERO(&wt->wfds);
    FD_ZERO(&wt->efds);
    varray_iterate(&wt->rpcs, _aux_fdsets_cb, wt);
    vlock_leave(&wt->lock);

    {
        struct timespec tmo = {0, 5000*1000*100 };
        sigset_t sigmask;
        sigset_t origmask;
        int ret = 0;

        sigemptyset(&sigmask);
        sigemptyset(&origmask);
        pthread_sigmask(SIG_SETMASK, &sigmask, &origmask);
        ret = pselect(wt->maxfd, &wt->rfds, &wt->wfds, &wt->efds, &tmo, &sigmask);
        vlogEv((ret < 0), elog_pselect);
        retE((ret < 0));
        retS((!ret)); //timeout.
    }

    vlock_enter(&wt->lock);
    varray_iterate(&wt->rpcs, _aux_laundry_cb, wt);
    vlock_leave(&wt->lock);
    return 0;
}

static
int _aux_dump_cb(void* item, void* cookie)
{
    struct vrpc* rpc = (struct vrpc*)item;
    vassert(rpc);

    rpc->ops->dump(rpc);
    return 0;
}
/*
 * @wt:
 */
static
int _vwaiter_dump(struct vwaiter* wt)
{
    vassert(wt);

    vdump(printf("-> rpc list: "));
    vlock_enter(&wt->lock);
    varray_iterate(&wt->rpcs, _aux_dump_cb, wt);
    vlock_leave(&wt->lock);
    return 0;
}

static
struct vwaiter_ops waiter_ops = {
    .add     = _vwaiter_add,
    .remove  = _vwaiter_remove,
    .laundry = _vwaiter_laundry,
    .dump    = _vwaiter_dump
};

/*
 * @wt:
 */
int vwaiter_init(struct vwaiter* wt)
{
    vassert(wt);

    wt->reset = 1;
    wt->maxfd = 0;
    vlock_init(&wt->lock);
    varray_init(&wt->rpcs, 8);
    wt->ops = &waiter_ops;
    return 0;
}

/*
 * @wt:
 */
void vwaiter_deinit(struct vwaiter* wt)
{
    vassert(wt);

    varray_deinit(&wt->rpcs);
    vlock_deinit(&wt->lock);
    return ;
}

