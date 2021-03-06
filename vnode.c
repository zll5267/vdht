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

    vlock_enter(&node->lock);
    if (node->mode != VDHT_OFF) {
        vlock_leave(&node->lock);
        return 0;
    }
    ret = upnpc->ops->setup(upnpc);
    vlogEv((ret < 0), "upnpc setup error");

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

    upnpc->ops->shutdown(upnpc);
    node->mode = VDHT_DOWN;
    vlock_leave(&node->lock);
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
int _aux_node_get_uaddrs(struct vnode* node)
{
    struct vnode_addr_helper* helper = &node->addr_helper;
    struct vupnpc* upnpc = &node->upnpc;
    vnodeInfo* nodei = (vnodeInfo*)&node->nodei;
    struct sockaddr_in uaddr;
    int ret = 0;
    int i = 0;

    vassert(node);
    for (i = 0; i < helper->naddrs; i++) {
        ret = upnpc->ops->map(upnpc, &helper->addrs[i], UPNP_PROTO_UDP, &uaddr);
        if (ret < 0) {
            continue;
        }
        vnodeInfo_add_addr(&nodei, &uaddr);
    }
    return 0;
}

static
void _aux_node_get_eaddrs(struct vnode* node)
{
    struct vnode_addr_helper* helper = &node->addr_helper;
    int i = 0;
    vassert(node);

    for (i = 0; i < helper->naddrs; i++) {
        if (reflexive_mask_check(helper->mask, i)) {
            continue;
        }
        node->route->ops->reflex(node->route, &helper->addrs[i]);
    }
    return;
}

static
void _aux_node_probe_connectivity(struct vnode* node)
{
    struct vnode_addr_helper* helper = &node->addr_helper;
    int i = 0;

    vassert(node);

    for (i = 0; i < helper->naddrs; i++) {
        node->route->ops->probe_connectivity(node->route, &helper->addrs[i]);
    }
    return;
}

static
int _aux_node_load_boot_cb(struct sockaddr_in* boot_addr, void* cookie)
{
    struct vnode* node = (struct vnode*)cookie;
    int ret = 0;

    vassert(node);
    vassert(boot_addr);

    if (vnodeInfo_has_addr((vnodeInfo*)&node->nodei, boot_addr)) {
        return 0;
    }
    ret = node->route->ops->join_node(node->route, boot_addr);
    retE((ret < 0));
    return 0;
}

static
int _aux_node_tick_cb(void* cookie)
{
    struct vnode* node  = (struct vnode*)cookie;
    struct vconfig* cfg = node->cfg;
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
        (void)node->route->ops->load(node->route);
        (void)cfg->ext_ops->get_boot_nodes(cfg, _aux_node_load_boot_cb, node);
        _aux_node_get_uaddrs(node);
        _aux_node_get_eaddrs(node);

        node->ts   = now;
        node->mode = VDHT_RUN;
        vlogI("DHT start running");
        break;
    }
    case VDHT_RUN: {
        if (now - node->ts > node->tick_tmo) {
            node->route->ops->tick(node->route);
            node->ops->renice(node);
            node->ops->tick(node);
            node->ts = now;
        }
        break;
    }
    case VDHT_DOWN: {
        node->route->ops->store(node->route);
        node->mode = VDHT_OFF;
        vlogI("DHT become offline");
        break;
    }
    default:
        vassert(0);
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
    vdump(printf("-> node infos:"));
    vnodeInfo_dump((vnodeInfo*)&node->nodei);
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
        vsrvcInfo* srvci = (vsrvcInfo*)varray_pop_tail(&node->services);
        vsrvcInfo_free(srvci);
    }
    vlock_leave(&node->lock);
    return ;
}

/*
 * the routine to update the nice index ( the index of resource avaiblility)
 * of local host. The higher the nice value is, the lower availability of
 * resource of the node.
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
    _aux_node_get_eaddrs(node);
    _aux_node_probe_connectivity(node);
    for (i = 0; i < varray_size(&node->services); i++) {
        svc = (vsrvcInfo*)varray_get(&node->services, i);
        route->ops->air_service(route, svc);
    }
    vlock_leave(&node->lock);
    return ;
}

static
int _vnode_myself(struct vnode* node, vnodeInfo_relax* nodei)
{
    int ret = 0;

    vassert(node);
    vassert(nodei);

    memset(nodei, 0, sizeof(*nodei));
    nodei->capc   = VNODEINFO_MAX_ADDRS;

    ret = vnodeInfo_copy((vnodeInfo*)nodei, (vnodeInfo*)&node->nodei);
    retE((ret < 0));
    return 0;
}

/*
 * the routine to update reflexive address.
 * @node
 */
static
int _vnode_reflex_addr(struct vnode* node, struct sockaddr_in* laddr, struct sockaddr_in* eaddr)
{
    struct vnode_addr_helper* helper = &node->addr_helper;
    vnodeInfo* nodei = (vnodeInfo*)&node->nodei;
    int i = 0;
    vassert(node);
    vassert(eaddr);

    vlock_enter(&node->lock);
    for (i = 0; i < helper->naddrs; i++) {
        if (!vsockaddr_equal(laddr, &helper->addrs[i])) {
            continue;
        }
        if (reflexive_mask_check(helper->mask, i)) {
            break;
        }
        vnodeInfo_add_addr(&nodei, eaddr);
        reflexive_mask_set(helper->mask, i);
        break;
    }
    vlock_leave(&node->lock);
    return 0;
}

/*
 * the routine to check whether node contains the addr given by @addr
 * @node:
 * @addr:
 */
static
int _vnode_has_addr(struct vnode* node, struct sockaddr_in* addr)
{
    int found = 0;

    vassert(node);
    vassert(addr);

    vlock_enter(&node->lock);
    found = vnodeInfo_has_addr((vnodeInfo*)&node->nodei, addr);
    vlock_leave(&node->lock);

    return found;
}

static
struct vnode_ops node_ops = {
    .start         = _vnode_start,
    .stop          = _vnode_stop,
    .wait_for_stop = _vnode_wait_for_stop,
    .stabilize     = _vnode_stabilize,
    .dump          = _vnode_dump,
    .clear         = _vnode_clear,
    .renice        = _vnode_renice,
    .tick          = _vnode_tick,
    .myself        = _vnode_myself,
    .reflex_addr   = _vnode_reflex_addr,
    .has_addr      = _vnode_has_addr,
};


/*
 * the routine to post a service info (only contain meta info) as local
 * service, and this service will be broadcasted to all nodes in routing table.
 *
 * @node:
 * @hash
 * @addr:  address of service to provide.
 *
 */
static
int _vnode_srvc_post(struct vnode* node, vsrvcHash* hash, struct sockaddr_in* addr)
{
    vsrvcInfo* srvci = NULL;
    int found = 0;
    int i = 0;

    vassert(node);
    vassert(hash);
    vassert(addr);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->services); i++){
        srvci = (vsrvcInfo*)varray_get(&node->services, i);
        if (vtoken_equal(&srvci->hash, hash)) {
            vsrvcInfo_add_addr(&srvci, addr);
            found = 1;
            break;
        }
    }
    if (!found) {
        srvci = vsrvcInfo_alloc();
        vlogEv((!srvci), elog_vsrvcInfo_alloc);
        ret1E((!srvci), vlock_leave(&node->lock));

        vsrvcInfo_init(srvci, hash, &node->nodei.id, node->nice);
        vsrvcInfo_add_addr(&srvci, addr);
        varray_add_tail(&node->services, srvci);
        node->nodei.weight++;
    }
    vlock_leave(&node->lock);
    return 0;
}

/*
 * the routine to unpost a posted service info;
 *
 * @node:
 * @hash: hash of service.
 * @addr: address of service to provide.
 *
 */
static
int _vnode_srvc_unpost(struct vnode* node, vsrvcHash* hash, struct sockaddr_in* addr)
{
    vsrvcInfo* srvci = NULL;
    int found = 0;
    int i = 0;

    vassert(node);
    vassert(addr);
    vassert(hash);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->services); i++) {
        srvci = (vsrvcInfo*)varray_get(&node->services, i);
        if (vtoken_equal(&srvci->hash, hash)) {
            vsrvcInfo_del_addr(srvci, addr);
            found = 1;
            break;
        }
    }
    if ((found) && (vsrvcInfo_is_empty(srvci))) {
        srvci = (vsrvcInfo*)varray_del(&node->services, i);
        vsrvcInfo_free(srvci);
        node->nodei.weight--;
    }
    vlock_leave(&node->lock);
    return 0;
}

static
int _vnode_srvc_unpost_ext(struct vnode* node, vsrvcHash* hash)
{
    vsrvcInfo* srvci = NULL;
    int found = 0;
    int i = 0;

    vassert(node);
    vassert(hash);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->services); i++) {
        srvci = (vsrvcInfo*)varray_get(&node->services, i);
        if (vtoken_equal(&srvci->hash, hash)) {
            found = 1;
            break;
        }
    }
    if (found) {
        srvci = (vsrvcInfo*)varray_del(&node->services, i);
        vsrvcInfo_free(srvci);
        node->nodei.weight--;
    }
    vlock_leave(&node->lock);
    return 0;
}

static
int _vnode_srvc_find(struct vnode* node, vsrvcHash* hash, vsrvcInfo_number_addr_t ncb, vsrvcInfo_iterate_addr_t icb, void* cookie)
{
    struct vroute* route = node->route;
    vsrvcInfo_relax srvci_relax;
    vsrvcInfo* srvci = NULL;
    int found = 0;
    int ret = 0;
    int i = 0;

    vassert(node);
    vassert(hash);
    vassert(ncb);
    vassert(icb);

    vlock_enter(&node->lock);
    for (i = 0; i < varray_size(&node->services); i++) {
        srvci = (vsrvcInfo*)varray_get(&node->services, i);
        if (vtoken_equal(&srvci->hash, hash)) {
            found = 1;
            break;
        }
    }
    if (found) {
        memset(&srvci_relax, 0, sizeof(srvci_relax));
        srvci_relax.capc = VSRVCINFO_MAX_ADDRS;
        vsrvcInfo_copy((vsrvcInfo*)&srvci_relax, srvci);
    }
    vlock_leave(&node->lock);

    if (found) {
        ncb(hash, srvci_relax.naddrs, VPROTO_UDP, cookie);
        for (i = 0; i < srvci_relax.naddrs; i++) {
            icb(hash, &srvci_relax.addrs[i], (i+1) == srvci_relax.naddrs, cookie);
        }
        return 0;
    }

    ret = route->ops->find_service(route, hash, ncb, icb, cookie);
    retE((ret < 0));
    return 0;
}

static
int _vnode_srvc_probe(struct vnode* node, vsrvcHash* hash, vsrvcInfo_number_addr_t ncb, vsrvcInfo_iterate_addr_t icb, void* cookie)
{
    struct vroute* route = node->route;
    int ret = 0;

    vassert(node);
    vassert(hash);
    vassert(ncb);
    vassert(icb);

    ret = route->ops->probe_service(route, hash, ncb, icb, cookie);
    retE((ret < 0));
    return 0;
}

static
struct vnode_srvc_ops node_srvc_ops = {
    .post       = _vnode_srvc_post,
    .unpost     = _vnode_srvc_unpost,
    .unpost_ext = _vnode_srvc_unpost_ext,
    .find       = _vnode_srvc_find,
    .probe      = _vnode_srvc_probe
};

static
int _aux_node_get_local_addrs(struct vnode_addr_helper* helper, struct vconfig* cfg)
{
    struct sockaddr_in laddr;
    char ip[64];
    int port = 0;
    int ret  = 0;

    vassert(cfg);
    vassert(helper);

    helper->naddrs = 0;
    helper->mask   = 0;

    port = cfg->ext_ops->get_dht_port(cfg);
    memset(ip, 0, 64);
    ret = vhostaddr_get_first(ip, 64);
    retE((ret < 0));
    vsockaddr_convert(ip, port, &laddr);
    vsockaddr_copy(&helper->addrs[helper->naddrs], &laddr);
    if (vsockaddr_is_private(&laddr)) {
        unreflexive_mask_set(helper->mask, helper->naddrs);
    }
    helper->naddrs++;

    while (helper->naddrs <= VNODE_MAX_LOCAL_ADDRS) {
        memset(ip, 0, 64);
        ret = vhostaddr_get_next(ip, 64);
        if (ret <= 0) {
            break;
        }
        vsockaddr_convert(ip, port, &laddr);
        vsockaddr_copy(&helper->addrs[helper->naddrs], &laddr);
        if (vsockaddr_is_private(&laddr)) {
            unreflexive_mask_set(helper->mask, helper->naddrs);
        }
        helper->naddrs++;
    }
    return 0;
}

static
int _aux_node_get_nodeinfo(vnodeInfo_relax* nodei, vnodeId* myid, struct vnode_addr_helper* helper)
{
    vnodeVer ver;
    int i = 0;

    vassert(myid);
    vassert(helper);

    vnodeVer_unstrlize(vhost_get_version(), &ver);
    vnodeInfo_relax_init(nodei, myid, &ver, 0);

    for (i = 0; i < helper->naddrs; i++) {
        vnodeInfo_add_addr((vnodeInfo**)&nodei, &helper->addrs[i]);
    }
    return 0;
}

/*
 * @node:
 * @msger:
 * @ticker:
 * @addr:
 */
int vnode_init(struct vnode* node, struct vconfig* cfg, struct vhost* host, vnodeId* myid)
{
    int ret = 0;

    vassert(node);
    vassert(cfg);
    vassert(host);
    vassert(myid);

    ret = _aux_node_get_local_addrs(&node->addr_helper, cfg);
    retE((ret < 0));
    ret = _aux_node_get_nodeinfo(&node->nodei, myid, &node->addr_helper);
    retE((ret < 0));

    vlock_init(&node->lock);
    node->mode  = VDHT_OFF;

    node->nice = 5;
    varray_init(&node->services, 4);
    vnode_nice_init(&node->node_nice, cfg);
    vupnpc_init(&node->upnpc);

    node->cfg     = cfg;
    node->ticker  = &host->ticker;
    node->route   = &host->route;
    node->ops     = &node_ops;
    node->srvc_ops= &node_srvc_ops;
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

