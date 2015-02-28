#include "vglobal.h"
#include "vnode.h"

enum vnode_mode {
    VDHT_OFF,
    VDHT_UP,
    VDHT_RUN,
    VDHT_DOWN,
    VDHT_ERR,
    VDHT_M_BUTT
};

static
char* node_mode_desc[] = {
    "offline",
    "up",
    "running",
    "down",
    "error",
    NULL
};

/*
 * start current dht node.
 * @node:
 */
static
int _vnode_start(struct vnode* node)
{
    struct vupnpc* upnpc = &node->upnpc;
    int ret = 0;
    vassert(node);

    ret = upnpc->ops->setup(upnpc);
    vlog((ret < 0), vlogI(printf("upnpc setup error")));

    vlock_enter(&node->lock);
    if (node->mode != VDHT_OFF) {
        vlock_leave(&node->lock);
        return 0;
    }
    node->mode = VDHT_UP;
    vlock_leave(&node->lock);
    return 0;
}

/*
 * @node
 */
static
int _vnode_stop(struct vnode* node)
{
    struct vupnpc* upnpc = &node->upnpc;
    vassert(node);

    vlock_enter(&node->lock);
    if ((node->mode == VDHT_OFF) || (node->mode == VDHT_ERR)) {
        vlock_leave(&node->lock);
        return 0;
    }
    node->mode = VDHT_DOWN;
    vlock_leave(&node->lock);

    upnpc->ops->shutdown(upnpc);
    return 0;
}

static
int _vnode_wait_for_stop(struct vnode* node)
{
    vassert(node);

    vlock_enter(&node->lock);
    while (node->mode != VDHT_OFF) {
        vlock_leave(&node->lock);
        sleep(1);
        vlock_enter(&node->lock);
    }
    vlock_leave(&node->lock);

    return 0;
}

static
void _aux_node_get_uaddrs(struct vnode* node)
{
    struct vupnpc* upnpc = &node->upnpc;
    struct sockaddr_in uaddr;
    vnodeInfo* ni = NULL;
    int ret = 0;
    int i = 0;

    vassert(node);

    for (i = 0; i < varray_size(&node->nodeinfos); i++) {
        ni = (vnodeInfo*)varray_get(&node->nodeinfos, i);
        if (vsockaddr_is_public(&ni->laddr)) {
            // if being public side, then need not.
            continue;
        }
        ret = upnpc->ops->map(upnpc, &ni->laddr, UPNP_PROTO_UDP, &uaddr);
        if (ret < 0) {
            continue;
        }
        vnodeInfo_set_uaddr(ni, &uaddr);
        if (vsockaddr_is_public(&uaddr)) {
            vnodeInfo_set_eaddr(ni, &uaddr);
        }
    }
    return;
}

static
void _aux_node_get_eaddrs(struct vnode* node)
{
    struct vroute* route = node->route;
    vassert(node);

    if (node->main_node_info->addr_flags & VNODEINFO_EADDR) {
        return;
    }
    route->ops->reflect(route);
    return;
}

static
int _aux_node_load_boot_cb(struct sockaddr_in* boot_addr, void* cookie)
{
    struct vnode*  node  = (struct vnode*)cookie;
    struct vroute* route = node->route;
    int ret = 0;

    vassert(boot_addr);
    retS((node->ops->is_self(node, boot_addr)));

    ret = route->ops->join_node(route, boot_addr);
    retE((ret < 0));
    return 0;
}

static
int _aux_node_tick_cb(void* cookie)
{
    struct vnode* node   = (struct vnode*)cookie;
    struct vroute* route = node->route;
    struct vconfig* cfg  = node->cfg;
    time_t now = time(NULL);
    vassert(node);

    vlock_enter(&node->lock);
    switch (node->mode) {
    case VDHT_OFF:
    case VDHT_ERR:  {
        //do nothing;
        break;
    }
    case VDHT_UP: {
        (void)_aux_node_get_uaddrs(node);
        (void)route->ops->load(route);
        (void)cfg->ext_ops->get_boot_nodes(cfg, _aux_node_load_boot_cb, node);

        node->ts   = now;
        node->mode = VDHT_RUN;
        vlogI(printf("DHT start running"));
        break;
    }
    case VDHT_RUN: {
        (void)_aux_node_get_eaddrs(node);
        if (now - node->ts > node->tick_tmo) {
            route->ops->tick(route);
            node->ops->tick(node);
            node->ts = now;
        }
        break;
    }
    case VDHT_DOWN: {
        node->route->ops->store(node->route);
        node->mode = VDHT_OFF;
        vlogI(printf("DHT become offline"));
        break;
    }
    default:
        vassert(0);
    }
    vlock_leave(&node->lock);
    return 0;
}

static
int _vnode_set_eaddr(struct vnode* node, struct sockaddr_in* eaddr)
{
    vlock_enter(&node->lock);
    if (!(node->main_node_info->addr_flags & VNODEINFO_EADDR)) {
        vnodeInfo_set_eaddr(node->main_node_info, eaddr);
    }
    vlock_leave(&node->lock);

    return 0;
}

/*
 * @node
 */
static
int _vnode_stabilize(struct vnode* node)
{
    struct vticker* ticker = node->ticker;
    int ret = 0;

    vassert(node);
    vassert(ticker);

    ret = ticker->ops->add_cb(ticker, _aux_node_tick_cb, node);
    retE((ret < 0));
    return 0;
}

/*
 * @node:
 */
static
void _vnode_dump(struct vnode* node)
{
    int i = 0;
    vassert(node);

    vdump(printf("-> NODE"));
    vlock_enter(&node->lock);
    vdump(printf("-> state:%s", node_mode_desc[node->mode]));
    if (varray_size(&node->nodeinfos) > 0) {
        vdump(printf("-> list of nodes:"));
        for (i = 0; i < varray_size(&node->nodeinfos); i++) {
            vnodeInfo* ninfo = (vnodeInfo*)varray_get(&node->nodeinfos, i);
            printf("{ ");
            vnodeInfo_dump(ninfo);
            printf(" }\n");
        }
    }
    if (varray_size(&node->services) > 0) {
        vdump(printf("-> list of services:"));
        for (i = 0; i < varray_size(&node->services); i++) {
            vsrvcInfo* svc = (vsrvcInfo*)varray_get(&node->services, i);
            printf("{ ");
            vsrvcInfo_dump(svc);
            printf(" }\n");
        }
    }
    vlock_leave(&node->lock);
    vdump(printf("<- NODE"));
    return ;
}

/*
 * the routine to clear all registered service infos.
 *
 * @node:
 */
static
void _vnode_clear(struct vnode* node)
{
    vassert(node);

    vlock_enter(&node->lock);
    while(varray_size(&node->services) > 0) {
        vsrvcInfo* svc = (vsrvcInfo*)varray_pop_tail(&node->services);
        vsrvcInfo_free(svc);
    }
    vlock_leave(&node->lock);
    return ;
}

/*
 * @node
 */
static
struct sockaddr_in* _vnode_get_best_usable_addr(struct vnode* node, vnodeInfo* dest)
{
    struct sockaddr_in* addr = NULL;
    vassert(node);
    vassert(dest);

    vlock_enter(&node->lock);
    if ((!addr) && (dest->addr_flags & VNODEINFO_EADDR)) {
        if (node->main_node_info->addr_flags & VNODEINFO_EADDR) {
            if (!vsockaddr_equal(&node->main_node_info->eaddr, &dest->eaddr)) {
                addr = &dest->eaddr;
            }
        } else {
            addr = &dest->eaddr;
        }
    }

    if ((!addr) && (dest->addr_flags & VNODEINFO_UADDR)) {
        if (node->main_node_info->addr_flags & VNODEINFO_UADDR) {
            if (!vsockaddr_equal(&node->main_node_info->uaddr, &dest->uaddr)) {
                addr = &dest->uaddr;
            }
        } else {
            addr = &dest->uaddr;
        }
    }
    if ((!addr) && (dest->addr_flags & VNODEINFO_LADDR)) {
        addr = &dest->laddr;
    }
    vlock_leave(&node->lock);
    return addr;
}

static
int _vnode_self(struct vnode* node, vnodeInfo* ni)
{
    vassert(node);
    vassert(ni);

    vlock_enter(&node->lock);
    vnodeInfo_copy(ni, node->main_node_info);
    vlock_leave(&node->lock);

    return 0;
}

static
int _vnode_is_self(struct vnode* node, struct sockaddr_in* addr)
{
    vnodeInfo* ni = NULL;
    int yes = 0;
    int i = 0;
    vassert(node);
    vassert(addr);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->nodeinfos); i++) {
        ni = (vnodeInfo*)varray_get(&node->nodeinfos, i);
        yes += vnodeInfo_has_addr(ni, addr);
    }
    vlock_leave(&node->lock);

    return yes;
}

/*
 * the routine to register a service info (only contain meta info) as local
 * service, and the service will be published to all nodes in routing table.
 *
 * @node:
 * @srvcId: service Id.
 * @addr:  address of service to provide.
 *
 */
static
int _vnode_reg_service(struct vnode* node, vsrvcId* srvcId, struct sockaddr_in* addr)
{
    vsrvcInfo* svc = NULL;
    int found = 0;
    int i = 0;

    vassert(node);
    vassert(addr);
    vassert(srvcId);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->services); i++){
        svc = (vsrvcInfo*)varray_get(&node->services, i);
        if (vtoken_equal(&svc->id, srvcId)) {
            vsrvcInfo_add_addr(svc, addr);
            found = 1;
            break;
        }
    }
    if (!found) {
        svc = vsrvcInfo_alloc();
        vlog((!svc), elog_vsrvcInfo_alloc);
        ret1E((!svc), vlock_leave(&node->lock));
        vsrvcInfo_init(svc, srvcId, node->nice);
        vsrvcInfo_add_addr(svc, addr);
        varray_add_tail(&node->services, svc);
        //node->own_node.weight++;
    }
    vlock_leave(&node->lock);
    return 0;
}

/*
 * the routine to unregister or nulify a service info, which registered before.
 *
 * @node:
 * @srvcId: service hash Id.
 * @addr: address of service to provide.
 *
 */
static
void _vnode_unreg_service(struct vnode* node, vtoken* srvcId, struct sockaddr_in* addr)
{
    vsrvcInfo* svc = NULL;
    int found = 0;
    int i = 0;

    vassert(node);
    vassert(addr);
    vassert(srvcId);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->services); i++) {
        svc = (vsrvcInfo*)varray_get(&node->services, i);
        if (vtoken_equal(&svc->id, srvcId)) {
            vsrvcInfo_del_addr(svc, addr);
            found = 1;
            break;
        }
    }
    if ((found) && (vsrvcInfo_is_empty(svc))) {
        svc = (vsrvcInfo*)varray_del(&node->services, i);
        vsrvcInfo_free(svc);
        //node->own_node.weight--;
    }
    vlock_leave(&node->lock);
    return;
}

/*
 * the routine to update the nice index ( the index of resource avaiblility)
 * of local host. The higher the nice value is, the lower availability for
 * other nodes can provide.
 *
 * @node:
 * @nice : an index of resource availability.
 *
 */
static
int _vnode_renice(struct vnode* node)
{
    struct vnode_nice* node_nice = &node->node_nice;
    vsrvcInfo* svc = NULL;
    int nice = 0;
    int i = 0;

    vassert(node);

    vlock_enter(&node->lock);
    node->nice = node_nice->ops->get_nice(node_nice);
    for (i = 0; i < varray_size(&node->services); i++) {
        svc = (vsrvcInfo*)varray_get(&node->services, i);
        svc->nice = nice;
    }
    vlock_leave(&node->lock);
    return 0;
}

static
void _vnode_tick(struct vnode* node)
{
    struct vroute* route = node->route;
    vsrvcInfo* svc = NULL;
    int i = 0;
    vassert(node);

    vlock_enter(&node->lock);
    for (i= 0; i < varray_size(&node->services); i++) {
        svc = (vsrvcInfo*)varray_get(&node->services, i);
        route->ops->post_service(route, svc);
    }
    vlock_leave(&node->lock);
    return ;
}

static
struct vnode_ops node_ops = {
    .start                = _vnode_start,
    .stop                 = _vnode_stop,
    .wait_for_stop        = _vnode_wait_for_stop,
    .stabilize            = _vnode_stabilize,
    .set_eaddr            = _vnode_set_eaddr,
    .dump                 = _vnode_dump,
    .clear                = _vnode_clear,
    .reg_service          = _vnode_reg_service,
    .unreg_service        = _vnode_unreg_service,
    .renice               = _vnode_renice,
    .tick                 = _vnode_tick,
    .get_best_usable_addr = _vnode_get_best_usable_addr,
    .self                 = _vnode_self,
    .is_self              = _vnode_is_self
};

static
int _aux_node_get_nodeinfos(struct vconfig* cfg, vnodeId* my_id, struct varray* nodes, vnodeInfo** main_node_info)
{
    vnodeInfo* ni = NULL;
    struct sockaddr_in laddr;
    vnodeVer node_ver;
    char ip[64];
    int port = 0;
    int ret = 0;

    vassert(cfg);
    vassert(my_id);
    vassert(nodes);

    port = cfg->ext_ops->get_dht_port(cfg);
    vnodeVer_unstrlize(vhost_get_version(), &node_ver);

    memset(ip, 0, 64);
    ret = vhostaddr_get_first(ip, 64);
    retE((ret <= 0));
    vsockaddr_convert(ip, port, &laddr);

    ni = vnodeInfo_alloc();
    vlog((!ni), elog_vnodeInfo_alloc);
    retE((!ni));

    vnodeInfo_init(ni, my_id, &node_ver, 0);
    vnodeInfo_set_laddr(ni, &laddr);
    if (vsockaddr_is_public(&laddr)) {
        vnodeInfo_set_eaddr(ni, &laddr);
    }
    varray_add_tail(nodes, ni);

    *main_node_info = ni;

    while (1) {
        memset(ip, 0, 64);
        ret = vhostaddr_get_next(ip, 64);
        if (ret <= 0) {
            break;
        }
        vsockaddr_convert(ip, port, &laddr);

        ni = vnodeInfo_alloc();
        if (!ni) {
            vlog((!ni), elog_vnodeInfo_alloc);
            break;
        }
        vnodeInfo_init(ni, my_id, &node_ver, 0);
        vnodeInfo_set_laddr(ni, &laddr);
        if (vsockaddr_is_public(&laddr)) {
            vnodeInfo_set_eaddr(ni, &laddr);
        }
        varray_add_tail(nodes, ni);
    }
    return 0;
}

/*
 * @node:
 * @msger:
 * @ticker:
 * @addr:
 */
int vnode_init(struct vnode* node, struct vconfig* cfg, struct vhost* host, vnodeId* my_id)
{
    int ret = 0;

    vassert(node);
    vassert(cfg);
    vassert(host);
    vassert(my_id);

    vlock_init(&node->lock);
    node->mode  = VDHT_OFF;

    node->nice = 5;
    varray_init(&node->nodeinfos, 2);
    varray_init(&node->services, 4);
    ret = _aux_node_get_nodeinfos(cfg, my_id, &node->nodeinfos, &node->main_node_info);
    retE((ret < 0));

    vnode_nice_init(&node->node_nice, cfg);
    vupnpc_init(&node->upnpc);

    node->cfg     = cfg;
    node->ticker  = &host->ticker;
    node->route   = &host->route;
    node->ops     = &node_ops;
    node->tick_tmo= cfg->ext_ops->get_host_tick_tmo(cfg);

    return 0;
}

/*
 * @node:
 */
void vnode_deinit(struct vnode* node)
{
    vassert(node);

    node->ops->clear(node);
    varray_deinit(&node->services);

    node->ops->wait_for_stop(node);
    vupnpc_deinit(&node->upnpc);
    vnode_nice_deinit(&node->node_nice);
    vlock_deinit (&node->lock);

    return ;
}

