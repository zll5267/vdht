#include "vglobal.h"
#include "vdht.h"

struct vdhtId_desc {
    int   id;
    char* desc;
};

static
struct vdhtId_desc dhtId_desc[] = {
    {VDHT_PING,                 "ping"                  },
    {VDHT_PING_R,               "ping_rsp"              },
    {VDHT_FIND_NODE,            "find_node"             },
    {VDHT_FIND_NODE_R,          "find_node_rsp"         },
    {VDHT_FIND_CLOSEST_NODES,   "find_closest_nodes"    },
    {VDHT_FIND_CLOSEST_NODES_R, "find_closest_nodes_rsp"},
    {VDHT_REFLECT,              "reflect"               },
    {VDHT_REFLECT_R,            "reflect_rsp"           },
    {VDHT_POST_SERVICE,         "post_service"          },
    {VDHT_POST_HASH,            "post_hash"             },
    {VDHT_GET_PEERS,            "get_peers"             },
    {VDHT_GET_PEERS_R,          "get_peers_rsp"         },
    {VDHT_UNKNOWN, NULL}
};

char* vdht_get_desc(int dhtId)
{
    struct vdhtId_desc* desc = dhtId_desc;
    int i = 0;

    if ((dhtId < 0) || (dhtId >= VDHT_UNKNOWN)) {
        return "unknown dht";
    }

    for (; desc->desc; i++) {
        if (desc->id == dhtId) {
            break;
        }
        desc++;
    }
    return desc->desc;
}

int vdht_get_dhtId_by_desc(const char* dht_str)
{
    struct vdhtId_desc* desc = dhtId_desc;
    int dhtId = -1;

    vassert(dht_str);

    for (; desc->desc; desc++) {
        if (!strcmp(dht_str, desc->desc)) {
            dhtId = desc->id;
            break;
        }
    }
    return dhtId;
}

static
int  vdht_get_queryId(char* desc)
{
    struct vdhtId_desc* id_desc = dhtId_desc;
    int qId = VDHT_UNKNOWN;
    vassert(desc);

    for (; id_desc->desc;) {
        if (!strcmp(id_desc->desc, desc)) {
            qId = id_desc->id;
            break;
        }
        id_desc++;
    }
    return qId;
}

void* vdht_buf_alloc(void)
{
    void* buf = NULL;

    buf = malloc(8*BUF_SZ);
    vlog((!buf), elog_malloc);
    retE_p((!buf));
    memset(buf, 0, 8*BUF_SZ);

    return buf + 8;
}

int vdht_buf_len(void)
{
    return (int)(8*BUF_SZ - 8);
}

void vdht_buf_free(void* buf)
{
    vassert(buf);

    free(buf - 8);
    return ;
}

static
struct be_node* _aux_create_vnodeInfo(vnodeInfo* info)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    vassert(info);

    dict = be_create_dict();
    retE_p((!dict));

    node = be_create_vtoken(&info->id);
    be_add_keypair(dict, "id", node);
    node = be_create_ver(&info->ver);
    be_add_keypair(dict, "v", node);
    if (info->addr_flags & VNODEINFO_LADDR) {
        node = be_create_addr(&info->laddr);
        be_add_keypair(dict, "ml", node);
    }
    if (info->addr_flags & VNODEINFO_UADDR) {
        node = be_create_addr(&info->uaddr);
        be_add_keypair(dict, "mu", node);
    }
    if (info->addr_flags & VNODEINFO_EADDR) {
        node = be_create_addr(&info->eaddr);
        be_add_keypair(dict, "me", node);
    }
    node = be_create_int(info->weight);
    be_add_keypair(dict, "w", node);

    return dict;
}

static
struct be_node* _aux_create_vsrvcInfo(vsrvcInfo* info)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* addr = NULL;
    int i = 0;
    vassert(info);

    dict = be_create_dict();
    retE_p((!dict));

    node = be_create_vtoken(&info->id);
    be_add_keypair(dict, "id", node);

    node = be_create_list();
    for (i = 0; i < info->naddrs; i++) {
        addr = be_create_addr(&info->addr[i]);
        be_add_list(node, addr);
    }
    be_add_keypair(dict, "m", node);
    node = be_create_int(info->nice);
    be_add_keypair(dict, "n", node);

    return dict;
}

/*
 * @token:
 * @srcId: Id of node sending query.
 * @buf:
 * @len:
 *
 * ping Query = {"t":"deafc137da918b8cd9b95e72fef379a5b54c3f36",
 *               "y":"q",
 *               "q":"ping",
 *               "a":{"id":"dbfcc5576ca7f742c802930892de9a1fb521f391"}
 *              }
 * encoded = d1:t40:deafc137da918b8cd9b95e72fef379a5b54c3f361:y1:q1:q4:ping1:a
 *           d2:id40:dbfcc5576ca7f742c802930892de9a1fb521f391ee
 */
static
int _vdht_enc_ping(vtoken* token, vnodeId* srcId, void* buf, int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* id   = NULL;
    int ret = 0;

    vassert(token);
    vassert(srcId);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("q");
    be_add_keypair(dict, "y", node);

    node = be_create_str("ping");
    be_add_keypair(dict, "q", node);

    node = be_create_dict();
    id   = be_create_vtoken(srcId);
    be_add_keypair(node, "id", id);
    be_add_keypair(dict, "a", node);

    ret  = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId:  Id of node replying query.
 * @result: queried result
 * @buf:
 * @len:
 * response = {"t":"06d5613f3f43631c539db6f10fbd04b651a21844",
 *             "y":"r",
 *             "r":{"node" :{"id": "dbfcc5576ca7f742c802930892de9a1fb521f391",
 *                            "v": "0.0.0.1.0",
 *                            "ml": "192.168.4.125:12300",
 *                            "w": "0"
 *                          }
 *                 }
 *            }
 * encoded = d1:t40:06d5613f3f43631c539db6f10fbd04b651a218441:y1:r1:rd4:noded
 *           2:id40:dbfcc5576ca7f742c802930892de9a1fb521f3911:v9:0.0.0.1.0
 *           2:ml19:192.168.4.125:123001:wi0eeee
 *
 */
static
int _vdht_enc_ping_rsp(vtoken* token, vnodeInfo* result, void* buf, int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* rslt = NULL;
    int ret = 0;

    vassert(token);
    vassert(result);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("r");
    be_add_keypair(dict, "y", node);

    rslt = be_create_dict();
    node = _aux_create_vnodeInfo(result);
    be_add_keypair(rslt, "node", node);
    be_add_keypair(dict, "r", rslt);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId:  Id of node sending query.
 * @target: Id of queried node.
 * @buf:
 * @len:
 *
 * find_node Query = {"t":"9948eb5973da8f3c3c0a",
 *                    "y":"q",
 *                    "q":"find_node",
 *                    "a": {"id":"7ba29c1b9215a2e7621e",
 *                          "target":"7ba29c1b9215a2e7621"
 *                         }
 *                   }
 * bencoded =  d1:t20:9948eb5973da8f3c3c0a1:y1:q1:q9:find_node1:ad2:id20:7ba29c
 *             1b9215a2e7621e6:target20:7ba29c1b9215a2e7621eee
 */
static
int _vdht_enc_find_node(
        vtoken* token,
        vnodeId* srcId,
        vnodeId* targetId,
        void* buf,
        int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* tmp  = NULL;
    int ret = 0;

    vassert(token);
    vassert(srcId);
    vassert(targetId);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("q");
    be_add_keypair(dict, "y", node);

    node = be_create_str("find_node");
    be_add_keypair(dict, "q", node);

    node = be_create_dict();
    tmp  = be_create_vtoken(srcId);
    be_add_keypair(node, "id", tmp);
    tmp  = be_create_vtoken(targetId);
    be_add_keypair(node, "target", tmp);
    be_add_keypair(dict, "a", node);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId: Id of node replying query
 * @result: queried result.
 * @buf:
 * @sz:
 *
 *  response = {"t":"9948eb5973da8f3c3c0a",
 *              "y":"r",
 *              "r":{"id":"ce3dbcf618862baf69e8",
 *                  "node" :{"id": "7ba29c1b9215a2e7621e",
 *                            "m": "192.168.4.46:12300",
 *                            "v": "0.0.0.1.0",
 *                            "f": "0"
 *                          }
 *                 }
 *            }
 * bencoded = d1:t20:9948eb5973da8f3c3c0a1:y1:r1:rd2:id20:ce3dbcf618862baf69e84
 *            :noded2:id20:7ba29c1b9215a2e7621e1:m18:192.168.4.46:123001:v9:0.0
 *            .0.0.01:fi0eeee
 */
static
int _vdht_enc_find_node_rsp(
        vtoken* token,
        vnodeId* srcId,
        vnodeInfo* result,
        void* buf,
        int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* rslt = NULL;
    int ret = 0;

    vassert(token);
    vassert(srcId);
    vassert(result);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("r");
    be_add_keypair(dict, "y", node);

    rslt = be_create_dict();
    node = be_create_vtoken(srcId);
    be_add_keypair(rslt, "id", node);

    node = _aux_create_vnodeInfo(result);
    be_add_keypair(rslt, "node", node);
    be_add_keypair(dict, "r", rslt);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId: Id of node sending query.
 * @target: Id of queried node.
 * @buf:
 * @len:
 *
 * find_node Query = {"t":"971ee80a808da2eb7fb2",
 *                    "y":"q",
 *                    "q":"find_closest_nodes",
 *                    "a": {"id":"7ba29c1b9215a2e7621e",
 *                          "target":"7ba29c1b9215a2e7621"
 *                         }
 *                   }
 * bencoded = :d1:t20:971ee80a808da2eb7fb21:y1:q1:q18:find_closest_nodes1:ad2:i
 *            d20:7ba29c1b9215a2e7621e6:target20:7ba29c1b9215a2e7621eee
 */
static
int _vdht_enc_find_closest_nodes(
        vtoken* token,
        vnodeId* srcId,
        vnodeId* targetId,
        void* buf,
        int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* tmp  = NULL;
    int ret = 0;

    vassert(token);
    vassert(srcId);
    vassert(targetId);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("q");
    be_add_keypair(dict, "y", node);

    node = be_create_str("find_closest_nodes");
    be_add_keypair(dict, "q", node);

    node = be_create_dict();
    tmp  = be_create_vtoken(srcId);
    be_add_keypair(node, "id", tmp);
    tmp  = be_create_vtoken(targetId);
    be_add_keypair(node, "target", tmp);
    be_add_keypair(dict, "a", node);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId: Id of node replying query
 * @result: queried result.
 * @buf:
 * @sz:
 *
 *  response = {"t":"30c6443e29cc307571e3",
 *              "y":"r",
 *              "r":{"id":"ce3dbcf618862baf69e8",
 *                  "nodes" :["id": "5a7f5578eace25999477",
 *                            "m":  "192.168.4.46:12300",
 *                            "v":  "0.0.0.0.01",
 *                            "f": "00000001"
 *                           ],
 *                           ...
 *                 }
 *            }
 */
static
int _vdht_enc_find_closest_nodes_rsp(
        vtoken* token,
        vnodeId* srcId,
        struct varray* closest,
        void* buf,
        int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* rslt = NULL;
    struct be_node* list = NULL;
    int ret = 0;
    int i   = 0;

    vassert(token);
    vassert(srcId);
    vassert(closest);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("r");
    be_add_keypair(dict, "y", node);

    rslt = be_create_dict();
    node = be_create_vtoken(srcId);
    be_add_keypair(rslt, "id", node);
    list = be_create_list();
    for (; i < varray_size(closest); i++) {
        vnodeInfo* info = NULL;

        info = (vnodeInfo*)varray_get(closest, i);
        node = _aux_create_vnodeInfo(info);
        be_add_list(list, node);
    }
    be_add_keypair(rslt, "nodes", list);
    be_add_keypair(dict, "r", rslt);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId: Id of node sending query.
 * @buf:
 * @len:
 * reflect Query = {"t":"deafc137da918b8cd9b95e72fef379a5b54c3f36",
 *                  "y":"q",
 *                  "q":"reflect",
 *                  "a":{"id":"dbfcc5576ca7f742c802930892de9a1fb521f391"}
 *              }
 * encoded = d1:t40:deafc137da918b8cd9b95e72fef379a5b54c3f361:y1:q1:q4:reflect
 *           1:ad2:id40:dbfcc5576ca7f742c802930892de9a1fb521f391ee
 */
static
int _vdht_enc_reflect(vtoken* token, vnodeId* srcId, void* buf, int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* id   = NULL;
    int ret = 0;

    vassert(token);
    vassert(srcId);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("q");
    be_add_keypair(dict, "y", node);

    node = be_create_str("reflect");
    be_add_keypair(dict, "q", node);

    node = be_create_dict();
    id   = be_create_vtoken(srcId);
    be_add_keypair(node, "id", id);
    be_add_keypair(dict, "a", node);

    ret  = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId: Id of node replying query
 * @result: queried result.
 * @buf:
 * @sz:
 *
 *  response = {"t":"9948eb5973da8f3c3c0a",
 *              "y":"r",
 *              "r":{"id":"ce3dbcf618862baf69e8",
 *                   "me" :"10.3.2.45:12300",
 *                 }
 *            }
 */
static
int _vdht_enc_reflect_rsp(
        vtoken* token,
        vnodeId* srcId,
        struct sockaddr_in* reflective_addr,
        void* buf,
        int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* rslt = NULL;
    int ret = 0;

    vassert(token);
    vassert(srcId);
    vassert(reflective_addr);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("r");
    be_add_keypair(dict, "y", node);

    rslt = be_create_dict();
    node = be_create_vtoken(srcId);
    be_add_keypair(rslt, "id", node);
    node = be_create_addr(reflective_addr);
    be_add_keypair(rslt, "me", node);
    be_add_keypair(dict, "r", rslt);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

/*
 * @token:
 * @srcId: Id of node sending query.
 * @target: Id of queried node.
 * @buf:
 * @len:
 *
 * Query = {"t":"7cf80a63748208c08ba5b97401d52fb3baf45cbe",",
 *          "y":"q",
 *          "q":"post_service",
 *          "a": {"id":"b790b74d9726c859c64cd54835d693b6019bcc73",
 *                "service" :{"id": "84d26f1cbea5d67d731a67c1b7ec427e40e948f9"
 *                            "m" : ["192.168.4.46:13500", "10.0.0.12:13500"]
 *                            "n" : "5"
 *                           }
 *               }
 *         }
 * encoded = d1:t40:7cf80a63748208c08ba5b97401d52fb3baf45cbe1:y1:q1:q12:post_service
 *           1:ad2:id40:b790b74d9726c859c64cd54835d693b6019bcc737:serviced2:id40:
 *           84d26f1cbea5d67d731a67c1b7ec427e40e948f91:ml18:192.168.4.46:1350015:
 *           10.0.0.12:13500e1:ni5eeee
 *
 */
static
int _vdht_enc_post_service(
        vtoken* token,
        vnodeId* srcId,
        vsrvcInfo* service,
        void* buf,
        int sz)
{
    struct be_node* dict = NULL;
    struct be_node* node = NULL;
    struct be_node* rslt = NULL;
    int ret = 0;

    vassert(token);
    vassert(service);
    vassert(buf);
    vassert(sz > 0);

    dict = be_create_dict();
    retE((!dict));

    node = be_create_vtoken(token);
    be_add_keypair(dict, "t", node);

    node = be_create_str("q");
    be_add_keypair(dict, "y", node);

    node = be_create_str("post_service");
    be_add_keypair(dict, "q", node);

    rslt = be_create_dict();
    node = be_create_vtoken(srcId);
    be_add_keypair(rslt, "id", node);

    node = _aux_create_vsrvcInfo(service);
    be_add_keypair(rslt, "service", node);
    be_add_keypair(dict, "a", rslt);

    ret = be_encode(dict, buf, sz);
    be_free(dict);
    retE((ret < 0));
    return ret;
}

static
int _vdht_enc_post_hash(
        vtoken* token,
        vnodeId* srcId,
        vnodeHash* hash,
        void* buf,
        int sz)
{
    vassert(token);
    vassert(srcId);
    vassert(hash);
    vassert(buf);
    vassert(sz > 0);

    //todo;
    return 0;
}

/*
 * @token :
 * @src : id of source node that send ping query.
 * @hash:
 * @buf:
 * @len:
 */
static
int _vdht_enc_get_peers(
        vtoken* token,
        vnodeId* srcId,
        vnodeHash* hash,
        void* buf,
        int sz)
{
    vassert(token);
    vassert(srcId);
    vassert(hash);
    vassert(buf);
    vassert(sz> 0);

    //todo;
    return 0;
}

/*
 * @token :
 * @srcId :
 * @result:
 * @buf:
 * @sz:
 */
static
int _vdht_enc_get_peers_rsp(
        vtoken* token,
        vnodeId* srcId,
        struct varray* result,
        void* buf,
        int sz)
{
    vassert(token);
    vassert(srcId);
    vassert(result);
    vassert(buf);
    vassert(sz > 0);
    //todo;
    return 0;
}

struct vdht_enc_ops dht_enc_ops = {
    .ping                   = _vdht_enc_ping,
    .ping_rsp               = _vdht_enc_ping_rsp,
    .find_node              = _vdht_enc_find_node,
    .find_node_rsp          = _vdht_enc_find_node_rsp,
    .find_closest_nodes     = _vdht_enc_find_closest_nodes,
    .find_closest_nodes_rsp = _vdht_enc_find_closest_nodes_rsp,
    .reflect                = _vdht_enc_reflect,
    .reflect_rsp            = _vdht_enc_reflect_rsp,
    .post_service           = _vdht_enc_post_service,
    .post_hash              = _vdht_enc_post_hash,
    .get_peers              = _vdht_enc_get_peers,
    .get_peers_rsp          = _vdht_enc_get_peers_rsp
};

static
int _aux_unpack_vtoken(struct be_node* dict, vtoken* token)
{
    struct be_node* node = NULL;
    int ret = 0;
    vassert(dict);
    vassert(token);

    ret = be_node_by_key(dict, "t", &node);
    retE((ret < 0));
    retE((BE_STR != node->type));

    ret  = be_unpack_token(node, token);
    retE((ret < 0));
    return 0;
}


static
int _aux_unpack_vnodeId(struct be_node* dict, char* key1, char* key2, vnodeId* id)
{
    struct be_node* node = NULL;
    int ret = 0;

    vassert(dict);
    vassert(key1);
    vassert(key2);
    vassert(id);

    ret = be_node_by_2keys(dict, key1, key2, &node);
    if ((ret < 0) || (BE_STR != node->type)) {
        return -1;
    }
    ret = be_unpack_token(node, id);
    retE((ret < 0));
    return 0;
}

static
int _aux_unpack_vnodeInfo(struct be_node* dict, vnodeInfo* info)
{
    struct be_node* node = NULL;
    struct sockaddr_in laddr, uaddr, eaddr;
    vnodeId  id;
    vnodeVer ver;
    int weight = 0;
    int ret = 0;

    vassert(dict);
    vassert(info);
    retE((BE_DICT != dict->type));

    ret = be_node_by_key(dict, "id", &node);
    retE((ret < 0));
    ret = be_unpack_token(node, &id);
    retE((ret < 0));

    ret = be_node_by_key(dict, "v", &node);
    retE((ret < 0));
    ret = be_unpack_ver(node, &ver);
    retE((ret < 0));

    ret = be_node_by_key(dict, "w", &node);
    retE((ret < 0));
    ret = be_unpack_int(node, &weight);
    retE((ret < 0));

    vnodeInfo_init(info, &id, &ver, weight);
    ret = be_node_by_key(dict, "ml", &node);
    if (ret >= 0) {
        ret = be_unpack_addr(node, &laddr);
        retE((ret < 0));
        vnodeInfo_set_laddr(info, &laddr);
    }
    ret = be_node_by_key(dict, "mu", &node);
    if (ret >= 0) {
        ret = be_unpack_addr(node, &uaddr);
        retE((ret < 0));
        vnodeInfo_set_uaddr(info, &uaddr);
    }
    ret = be_node_by_key(dict, "me", &node);
    if (ret >= 0) {
        ret = be_unpack_addr(node, &eaddr);
        retE((ret < 0));
        vnodeInfo_set_eaddr(info, &eaddr);
    }
    return 0;
}

static
int _aux_unpack_vsrvcInfo(struct be_node* dict, vsrvcInfo** info)
{
    struct vsrvcInfo* svc = NULL;
    struct be_node* node = NULL;
    vtoken id;
    int nice = 0;
    int i = 0;

    vassert(dict);
    vassert(info);
    retE((BE_DICT != dict->type));

    svc = vsrvcInfo_alloc();
    vlog((!svc), elog_vsrvcInfo_alloc);
    retE((!svc));

    be_node_by_key(dict, "id", &node);
    be_unpack_token(node, &id);
    be_node_by_key(dict, "n", &node);
    be_unpack_int(node, &nice);
    vsrvcInfo_init(svc, &id, nice);

    be_node_by_key(dict, "m", &node);
    vassert(node->type == BE_LIST);
    for (i = 0; node->val.l[i]; i++) {
        struct sockaddr_in addr;
        struct be_node* addr_node = node->val.l[i];
        be_unpack_addr(addr_node, &addr);
        vsrvcInfo_add_addr(svc, &addr);
    }

    *info = svc;
    return 0;
}

static
int _aux_unpack_dhtId(struct be_node* dict, vnodeId* srcId)
{
    struct be_node* node = NULL;
    int ret = 0;

    vassert(dict);
    retE((BE_DICT != dict->type));

    ret = be_node_by_key(dict, "y", &node);
    retE((ret < 0));
    retE((BE_STR != node->type));

    if (!strcmp(node->val.s, "q")) { //query.
        ret = _aux_unpack_vnodeId(dict, "a", "id", srcId);
        retE((ret < 0));

        ret = be_node_by_key(dict, "q", &node);
        retE((ret < 0));
        retE((BE_STR != node->type));
        return vdht_get_queryId(node->val.s);
    }
    if (!strcmp(node->val.s, "r")) { //response
        ret = _aux_unpack_vnodeId(dict, "r", "id", srcId);
        if (ret >= 0) {
            ret = be_node_by_2keys(dict, "r", "nodes", &node);
            if (!ret) {
                return VDHT_FIND_CLOSEST_NODES_R;
            }
            ret = be_node_by_2keys(dict, "r", "hash", &node);
            if (!ret) {
                return VDHT_GET_PEERS_R;
            }
            ret = be_node_by_2keys(dict, "r", "me", &node);
            if (!ret) {
                return VDHT_REFLECT_R;
            }
            ret = be_node_by_2keys(dict, "r", "node", &node);
            if (!ret) {
                return VDHT_FIND_NODE_R;
            }
            return VDHT_UNKNOWN;
        } else {
            ret = be_node_by_2keys(dict, "r", "node", &node);
            if (!ret) {
                return VDHT_PING_R;
            }
            return VDHT_UNKNOWN;
        }
    }
    return VDHT_UNKNOWN;
}

/*
 * @ctxt:  decoder context
 * @token:
 * @srcId: source nodeId
 *
 * ping Query = {"t":"deafc137da918b8cd9b95e72fef379a5b54c3f36",
 *               "y":"q",
 *               "q":"ping",
 *               "a":{"id":"dbfcc5576ca7f742c802930892de9a1fb521f391"}
 *              }
 * encoded = d1:t40:deafc137da918b8cd9b95e72fef379a5b54c3f361:y1:q1:q4:ping1:a
 *           d2:id40:dbfcc5576ca7f742c802930892de9a1fb521f391ee
 */
static
int _vdht_dec_ping(void* ctxt)
{
    //do nothing;
    vassert(ctxt);
    (void)ctxt;

    return 0;
}

/*
 * @ctxt:  decoder context
 * @token:
 * @result: node infos of destination node.
 *
 * response = {"t":"06d5613f3f43631c539db6f10fbd04b651a21844",
 *             "y":"r",
 *             "r":{"node" :{"id": "dbfcc5576ca7f742c802930892de9a1fb521f391",
 *                            "v": "0.0.0.1.0",
 *                            "ml": "192.168.4.125:12300",
 *                            "w": "0"
 *                          }
 *                 }
 *            }
 * encoded = d1:t40:06d5613f3f43631c539db6f10fbd04b651a218441:y1:r1:rd4:noded
 *           2:id40:dbfcc5576ca7f742c802930892de9a1fb521f3911:v9:0.0.0.1.0
 *           2:ml19:192.168.4.125:123001:wi0eeee
 */
static
int _vdht_dec_ping_rsp(void* ctxt, vnodeInfo* result)
{
    struct be_node* dict = (struct be_node*)ctxt;
    struct be_node* node = NULL;
    int ret = 0;

    vassert(ctxt);
    vassert(result);

    ret = be_node_by_2keys(dict, "r", "node", &node);
    retE((ret < 0));
    ret = _aux_unpack_vnodeInfo(node, result);
    retE((ret < 0));

    return 0;
}

/*
 * @ctxt: decoder context
 * @token:
 * @srcId: source nodeId
 * @targetId: target node Id
 *
 * find_node Query = {"t":"9948eb5973da8f3c3c0a",
 *                    "y":"q",
 *                    "q":"find_node",
 *                    "a": {"id":"7ba29c1b9215a2e7621e",
 *                          "target":"7ba29c1b9215a2e7621"
 *                         }
 *                   }
 * bencoded =  d1:t20:9948eb5973da8f3c3c0a1:y1:q1:q9:find_node1:ad2:id20:7ba29c
 *             1b9215a2e7621e6:target20:7ba29c1b9215a2e7621eee
 */
static
int _vdht_dec_find_node(void* ctxt, vnodeId* targetId)
{
    struct be_node* dict = (struct be_node*)ctxt;
    int ret = 0;

    vassert(dict);
    vassert(targetId);

    ret = _aux_unpack_vnodeId(dict, "a", "target", targetId);
    retE((ret < 0));
    return 0;
}

/*
 * @ctxt:
 * @token:
 * @srcId:
 * @result:
 *
  *  response = {"t":"9948eb5973da8f3c3c0a",
 *              "y":"r",
 *              "r":{"id":"ce3dbcf618862baf69e8",
 *                  "node" :{"id": "7ba29c1b9215a2e7621e",
 *                            "m": "192.168.4.46:12300",
 *                            "v": "0.0.0.1.0",
 *                            "f": "0"
 *                          }
 *                 }
 *            }
 * bencoded = d1:t20:9948eb5973da8f3c3c0a1:y1:r1:rd2:id20:ce3dbcf618862baf69e84
 *            :noded2:id20:7ba29c1b9215a2e7621e1:m18:192.168.4.46:123001:v9:0.0
 *            .0.0.01:fi0eeee
 */
static
int _vdht_dec_find_node_rsp(void* ctxt, vnodeInfo* result)
{
    struct be_node* dict = (struct be_node*)ctxt;
    struct be_node* node = NULL;
    int ret = 0;

    vassert(dict);
    vassert(result);

    ret = be_node_by_2keys(dict, "r", "node", &node);
    retE((ret < 0));
    ret = _aux_unpack_vnodeInfo(node, result);
    retE((ret < 0));

    return 0;
}

/*
 * @ctxt:
 * @token:
 * @srcId:
 * @closest:
 *
 * find_node Query = {"t":"971ee80a808da2eb7fb2",
 *                    "y":"q",
 *                    "q":"find_closest_nodes",
 *                    "a": {"id":"7ba29c1b9215a2e7621e",
 *                          "target":"7ba29c1b9215a2e7621"
 *                         }
 *                   }
 * bencoded = :d1:t20:971ee80a808da2eb7fb21:y1:q1:q18:find_closest_nodes1:ad2:i
 *            d20:7ba29c1b9215a2e7621e6:target20:7ba29c1b9215a2e7621eee
 */
static
int _vdht_dec_find_closest_nodes(void* ctxt, vnodeId* targetId)
{
    struct be_node* dict = (struct be_node*)ctxt;
    int ret = 0;

    vassert(dict);
    vassert(targetId);

    ret = _aux_unpack_vnodeId(dict, "a", "target", targetId);
    retE((ret < 0));
    return 0;
}

/*
 * @ctxt:  decoder context
 * @token: msg token;
 * @srcId: source vnodeId
 * @closest: array of nodes that close to current node.
 *
 *  response = {"t":"30c6443e29cc307571e3",
 *              "y":"r",
 *              "r":{"id":"ce3dbcf618862baf69e8",
 *                  "nodes" :["id": "5a7f5578eace25999477",
 *                            "m":  "192.168.4.46:12300",
 *                            "v":  "0.0.0.0.01",
 *                            "f": "00000001"
 *                           ],
 *                           ...
 *                 }
 *            }
 * bencoded = d1:t20:30c6443e29cc307571e31:y1:r1:rd2:id20:ce3dbcf618862baf69e8\
 *            5:nodesld2:id20:5a7f5578eace259994771:m18:192.168.4.46:123001:v\
 *            9:0.0.0.0.01:fi1417421748eeeee
 */
static
int _vdht_dec_find_closest_nodes_rsp(void* ctxt, struct varray* closest)
{
    struct be_node* dict = (struct be_node*)ctxt;
    struct be_node* list = NULL;
    struct be_node* node = NULL;
    int ret = 0;
    int i = 0;

    vassert(dict);
    vassert(closest);

    ret = be_node_by_2keys(dict, "r", "nodes", &list);
    retE((ret < 0));
    retE((BE_LIST != list->type));

    for (; list->val.l[i]; i++) {
        vnodeInfo* info = NULL;

        node = list->val.l[i];
        retE((BE_DICT != node->type));
        info = vnodeInfo_alloc();
        vlog((!info), elog_vnodeInfo_alloc);
        retE((!info));

        ret = _aux_unpack_vnodeInfo(node, info);
        ret1E((ret < 0), vnodeInfo_free(info));
        varray_add_tail(closest, info);
    }

    return 0;
}

static
int _vdht_dec_reflect(void* ctxt)
{
    //do nothing;
    vassert(ctxt);
    (void)ctxt;

    return 0;
}

static
int _vdht_dec_reflect_rsp(void* ctxt, struct sockaddr_in* reflective_addr)
{
    struct be_node* dict = (struct be_node*)ctxt;
    struct be_node* node = NULL;
    int ret = 0;

    vassert(dict);
    vassert(reflective_addr);

    ret = be_node_by_2keys(dict, "r", "me", &node);
    retE((ret < 0));
    ret = be_unpack_addr(node, reflective_addr);
    retE((ret < 0));

    return 0;
}

/* @ctxt:
 * @token:
 * @srcId:
 * @service:
 *
 * Query = {"t":"b6e0855abbf93b8e6754",",
 *          "y":"q",
 *          "q":"post_service",
 *          "a": {"id":"7ba29c1b9215a2e7621e",
 *                "service" :{"id": "e532d7c80cf02e8652d3"
 *                            "m" : ["192.168.4.46:14444", "27.115.62.114:15300"]
 *                            "f" : "0"
 *                           }
 *               }
 *         }
 * bencoded = d1:t20:b6e0855abbf93b8e67541:y1:q1:q12:post_service1:ad2:id20:
 *            7ba29c1b9215a2e7621e7:serviced2:id20:e532d7c80cf02e8652d31:m17:
 *            192.168.4.46:144441:fi0eeee
 */
static
int _vdht_dec_post_service(void* ctxt, vsrvcInfo** service)
{
    struct be_node* dict = (struct be_node*)ctxt;
    struct be_node* node = NULL;
    int ret = 0;

    vassert(dict);
    vassert(service);

    ret = be_node_by_2keys(dict, "a", "service", &node);
    retE((ret < 0));
    ret = _aux_unpack_vsrvcInfo(node, service);
    retE((ret < 0));

    return 0;
}

static
int _vdht_dec_post_hash(void* ctxt, vnodeHash* hash)
{
    vassert(ctxt);
    vassert(hash);

    //todo;
    return 0;
}

/*
 * @buf:
 * @sz:
 * @token:
 * @srcId:
 * @hash:
 */
static
int _vdht_dec_get_peers(void* ctxt, vnodeHash* hash)
{
    vassert(ctxt);
    vassert(hash);

    //todo;
    return 0;
}

/*
 * @buf:
 * @sz:
 * @token:
 * @srcId:
 * @result:
 */
static
int _vdht_dec_get_peers_rsp(void* ctxt, struct varray* result)
{
    vassert(ctxt);
    vassert(result);

    //todo;
    return 0;
}

static
int _vdht_dec_begin(void* buf, int len, vtoken* token, vnodeId* srcId, void** ctxt)
{
    struct be_node* dict = NULL;
    int ret = 0;

    vassert(buf);
    vassert(len);

    vlogI(printf("[dht msg]->%s", (char*)buf));
    dict = be_decode(buf, len);
    vlog((!dict), elog_be_decode);
    retE((!dict));

    ret = _aux_unpack_vtoken(dict, token);
    ret1E((ret < 0), be_free(dict));

    ret = _aux_unpack_dhtId(dict, srcId);
    ret1E((ret < 0), be_free(dict));

    *ctxt = (void*)dict;
    return ret;
}

static
int _vdht_dec_done(void* ctxt)
{
    vassert(ctxt);
    be_free((struct be_node*)ctxt);
    return 0;
}

struct vdht_dec_ops dht_dec_ops = {
    .dec_begin              = _vdht_dec_begin,
    .dec_done               = _vdht_dec_done,

    .ping                   = _vdht_dec_ping,
    .ping_rsp               = _vdht_dec_ping_rsp,
    .find_node              = _vdht_dec_find_node,
    .find_node_rsp          = _vdht_dec_find_node_rsp,
    .find_closest_nodes     = _vdht_dec_find_closest_nodes,
    .find_closest_nodes_rsp = _vdht_dec_find_closest_nodes_rsp,
    .reflect                = _vdht_dec_reflect,
    .reflect_rsp            = _vdht_dec_reflect_rsp,
    .post_service           = _vdht_dec_post_service,
    .post_hash              = _vdht_dec_post_hash,
    .get_peers              = _vdht_dec_get_peers,
    .get_peers_rsp          = _vdht_dec_get_peers_rsp
};

