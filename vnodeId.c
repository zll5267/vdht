#include "vglobal.h"
#include "vnodeId.h"

void vnodeId_make(vnodeId* id)
{
    char* data = NULL;
    int i = 0;
    vassert(id);

    srand(time(NULL)); // TODO: need use mac as srand seed.
    data = (char*)id->data;
    for (; i < VNODE_ID_LEN; i++) {
        data[i] = rand() % 9;
    }
    return ;
}

void vnodeId_dist(vnodeId* a, vnodeId* b, vnodeMetric* m)
{
    int i = 0;
    vassert(a);
    vassert(b);
    vassert(m);

    unsigned char* a_data = a->data;
    unsigned char* b_data = b->data;
    unsigned char* m_data = m->data;

    for (; i < VNODE_ID_LEN; i++) {
        *(m_data++) = *(a_data++) ^ *(b_data++);
    }
    return ;
}

static
int _aux_distance(vnodeMetric* m)
{
    int i = 0;
    vassert(m);

    for (; i < VNODE_ID_BITLEN; i++) {
        int bit  = VNODE_ID_BITLEN - i - 1;
        int byte = i / 8;
        int bbit = 7 - (i % 8);
        unsigned char comp = ( 1 << bbit);
        if (comp & m->data[byte]) {
            return bit;
        }
    }
    return 0;
}

int vnodeId_bucket(vnodeId* a, vnodeId* b)
{
    vnodeMetric m;
    vassert(a);
    vassert(b);

    vnodeId_dist(a, b, &m);
    return _aux_distance(&m);
}

int vnodeId_equal(vnodeId* a, vnodeId* b)
{
    int i = 0;
    vassert(a);
    vassert(b);

    for (; i < VNODE_ID_LEN; i++) {
        if (a->data[i] != b->data[i]) {
            return 0;
        }
    }
    return 1;
}

void vnodeId_dump(vnodeId* id)
{
    int i = 0;
    vassert(id);

    printf("##ID:");
    for (; i < VNODE_ID_LEN; i++) {
        printf("%c", id->data[i] + '0');
        if ((i % 4 == 3) && (i +1 != VNODE_ID_LEN)){
            printf("-");
        }
    }
    printf("\n");
    return;
}

void vnodeId_copy(vnodeId* dst, vnodeId* src)
{
    int i = 0;
    vassert(dst);
    vassert(src);

    for (; i < VNODE_ID_LEN; i++) {
        dst->data[i] = src->data[i];
    }
    return ;
}

int vnodeId_strlize(vnodeId* id, char* buf, int len)
{
    int i = 0;
    vassert(id);
    vassert(buf);
    vassert(len > 0);

    retE((len + 1 < VNODE_ID_LEN));
    for (; i < VNODE_ID_LEN; i++) {
        buf[i] = id->data[i] + '0';
    }
    buf[i] = '\0';
    return 0;
}

int vnodeId_unstrlize(const char* id_str, vnodeId* id)
{
    int i = 0;
    vassert(id_str);
    vassert(id);

    retE((strlen(id_str) != VNODE_ID_LEN));
    for(; i < VNODE_ID_LEN; i++) {
        retE((id_str[i] < '0'));
        retE((id_str[i] > '9'));
        id->data[i] = id_str[i] - '0';
    }
    return 0;
}

/*
 *
 */
int vnodeMetric_cmp(vnodeMetric* a, vnodeMetric* b)
{
    unsigned char* a_data = a->data;
    unsigned char* b_data = b->data;
    int i = 0;

    vassert(a);
    vassert(b);

    for(; i < VNODE_ID_LEN; i++){
        if (*a_data < *b_data) {
            return 1;
        } else if (*a_data > *b_data) {
            return -1;
        }
        a_data++;
        b_data++;
    }
    return 0;
}

/*
 * for vnode Addr.
 */
int  vnodeAddr_equal(vnodeAddr* a, vnodeAddr* b)
{
    vassert(a);
    vassert(b);

    if (vnodeId_equal(&a->id, &b->id) ||
        vsockaddr_equal(&a->addr,&b->addr)) {
        return 1;
    }
    return 0;
}

void vnodeAddr_copy (vnodeAddr* a, vnodeAddr* b)
{
    vassert(a);
    vassert(b);

    vnodeId_copy(&a->id, &b->id);
    vsockaddr_copy(&a->addr, &b->addr);
    return ;
}

void vnodeAddr_dump (vnodeAddr* addr)
{
    char ip[64];
    int port = 0;
    int ret = 0;

    vassert(addr);
    vnodeId_dump(&addr->id);
    ret = vsockaddr_unconvert(&addr->addr, ip, 64, &port);
    retE_v((ret < 0));
    printf("##Addr: %s:%d\n", ip, port);
    return ;
}

int vnodeAddr_init(vnodeAddr* nodeAddr, vnodeId* id, struct sockaddr_in* addr)
{
    vassert(nodeAddr);
    vassert(id);
    vassert(addr);

    vnodeId_copy(&nodeAddr->id, id);
    vsockaddr_copy(&nodeAddr->addr, addr);
    return 0;
}

/*
 * for dht node version.
 */
void vnodeVer_copy(vnodeVer* dst, vnodeVer* src)
{
    vassert(dst);
    vassert(src);

    for (int i = 0; i < VNODE_ID_LEN; i++) {
        dst->data[i] = src->data[i];
    }
    return ;
}

int vnodeVer_equal(vnodeVer* ver_a, vnodeVer* ver_b)
{
    int i = 0;
    vassert(ver_a);
    vassert(ver_b);

    for (; i < VNODE_ID_LEN; i++) {
        if (ver_a->data[i] != ver_b->data[i]) {
            return 0;
        }
    }
    return 1;
}

int vnodeVer_strlize(vnodeVer* ver, char* buf, int len)
{
    int offset = 0;
    int ret = 0;
    int i = 0;
    vassert(ver);
    vassert(buf);
    vassert(len > 0);

    ret = snprintf(buf, len - ret, "%d", ver->data[i]);
    retE((ret < 0));
    offset += ret;
    i++;

    for (; i < VNODE_ID_INTLEN; i++) {
        ret = snprintf(buf + offset, len - offset, ".");
        retE((ret < 0));
        offset += ret;

        ret = snprintf(buf + offset, len - offset, "%d", ver->data[i]);
        retE((ret < 0));
        offset += ret;
    }
    return 0;
}

int vnodeVer_unstrlize(const char* ver_str, vnodeVer* ver)
{
    char* s = (char*)ver_str;
    int32_t ret = 0;
    int i = 0;
    vassert(ver_str);
    vassert(ver);

    errno = 0;
    ret = strtol(s, &s, 10);
    retE((errno));
    ver->data[i] = ret;
    i++;

    while(*s != '\0') {
        retE((*s != '.'));
        s++;

        errno = 0;
        ret = strtol(s, &s, 10);
        retE((errno));
        ver->data[i] = ret;
        i++;
    }
    return 0;
}

void vnodeVer_dump(vnodeVer* ver)
{
    char sver[64];
    vassert(ver);

    memset(sver, 0, 64);
    vnodeVer_strlize(ver, sver, 64);
    vdump(printf("ver:%s", sver));

    return;
}

/*
 *  for token
 */
void vtoken_make(vtoken* token)
{
    char* data = NULL;
    int i = 0;
    vassert(token);

    srand(time(NULL)); // TODO: need use mac as srand seed.
    data = (char*)token->data;
    for (; i < VNODE_ID_LEN; i++) {
        data[i] = rand() % 9;
    }
    return ;

}

void vtoken_copy(vtoken* dst, vtoken* src)
{
    int i = 0;
    vassert(dst);
    vassert(src);

    for (; i < VNODE_ID_LEN; i++) {
        dst->data[i] = src->data[i];
    }
    return ;
}

int vtoken_equal(vtoken* a, vtoken* b)
{
    int i = 0;
    vassert(a);
    vassert(b);

    for (; i < VNODE_ID_LEN; i++) {
        if (a->data[i] != b->data[i]) {
            return 0;
        }
    }
    return 1;
}
int vtoken_strlize(vtoken* token, char* buf, int len)
{
    int i = 0;
    vassert(token);
    vassert(buf);
    vassert(len > 0);

    retE((len + 1 < VNODE_ID_LEN));
    for (; i < VNODE_ID_LEN; i++) {
        buf[i] = token->data[i] + '0';
    }
    buf[i] = '\0';
    return 0;
}

int vtoken_unstrlize(const char* token_str, vtoken* token)
{
    int i = 0;
    vassert(token_str);
    vassert(token);

    retE((strlen(token_str) != VNODE_ID_LEN));
    for(; i < VNODE_ID_LEN; i++) {
        retE((token_str[i] < '0'));
        retE((token_str[i] > '9'));
        token->data[i] = token_str[i] - '0';
    }
    return 0;
}

void vtoken_dump(vtoken* token)
{
    int i = 0;
    vassert(token);

    printf("##ID:");
    for (; i < VNODE_ID_LEN; i++) {
        printf("%c", token->data[i] + '0');
        if ((i % 4 == 3) && (i +1 != VNODE_ID_LEN)){
            printf("-");
        }
    }
    printf("\n");
    return;
}

/*
 *
 */
static MEM_AUX_INIT(nodeInfo_maux, sizeof(vnodeInfo), 0);
vnodeInfo* vnodeInfo_alloc(void)
{
    vnodeInfo* info = NULL;

    info = (vnodeInfo*)vmem_aux_alloc(&nodeInfo_maux);
    ret1E_p((!info), elog_vmem_aux_alloc);
    return info;
}

void vnodeInfo_free(vnodeInfo* info)
{
    vmem_aux_free(&nodeInfo_maux, info);
    return ;
}

void vnodeInfo_copy(vnodeInfo* dest, vnodeInfo* src)
{
    vassert(dest);
    vassert(src);

    vnodeId_copy(&dest->id, &src->id);
    vsockaddr_copy(&dest->addr, &src->addr);
    vnodeVer_copy(&dest->ver, &src->ver);
    dest->flags = src->flags;
    return ;
}

int vnodeInfo_init(vnodeInfo* info, vnodeId* id, struct sockaddr_in* addr, uint32_t flags, vnodeVer* ver)
{
    vassert(info);
    vassert(id);
    vassert(addr);
    vassert(ver || !ver);

    vnodeId_copy(&info->id, id);
    vsockaddr_copy(&info->addr, addr);
    if (!ver) {
        vnodeVer_unstrlize("0.0.0.0.0", &info->ver);
    } else {
        vnodeVer_copy(&info->ver, ver);
    }
    info->flags = flags;
    return 0;
}

void vnodeInfo_dump(vnodeInfo* info)
{
    vassert(info);

    vnodeId_dump(&info->id);
    vsockaddr_dump(&info->addr);
    vnodeVer_dump(&info->ver);
    vdump(printf("flags: 0x%x", info->flags));
    return ;
}

/*
 * for vserviceInfo
 */
void vserviceId_make(vserviceId* svcId)
{
    char* data = NULL;
    int i = 0;
    vassert(svcId);

    srand(time(NULL)); // TODO: need use mac as srand seed.
    data = (char*)svcId->data;
    for (; i < VNODE_ID_LEN; i++) {
        data[i] = rand() % 9;
    }
    return ;       
}

int vserviceId_strlize(vserviceId* id, char* buf, int len)
{
    int i = 0;
    vassert(id);
    vassert(buf);
    vassert(len > 0);

    retE((len + 1 < VNODE_ID_LEN));
    for (; i < VNODE_ID_LEN; i++) {
        buf[i] = id->data[i] + '0';
    }
    buf[i] = '\0';
    return 0;
}

int vserviceId_unstrlize(const char* id_str, vserviceId* id)
{
    int i = 0;
    vassert(id_str);
    vassert(id);

    retE((strlen(id_str) != VNODE_ID_LEN));
    for(; i < VNODE_ID_LEN; i++) {
        retE((id_str[i] < '0'));
        retE((id_str[i] > '9'));
        id->data[i] = id_str[i] - '0';
    }
    return 0;
}

void vserviceId_dump(vserviceId* svcId)
{
    int i = 0;
    vassert(svcId);

    printf("##Service ID:");
    for (; i < VNODE_ID_LEN; i++) {
        printf("%c", svcId->data[i] + '0');
        if ((i % 4 == 3) && (i +1 != VNODE_ID_LEN)){
            printf("-");
        }
    }
    printf("\n");
    return;       
}

static MEM_AUX_INIT(serviceInfo_maux, sizeof(vserviceInfo), 0);
vserviceInfo* vserviceInfo_alloc(void)
{
    vserviceInfo* info = NULL;

    info = (vserviceInfo*)vmem_aux_alloc(&serviceInfo_maux);
    ret1E_p((!info), elog_vmem_aux_alloc);
    return info;
}

void vserviceInfo_free(vserviceInfo* svc_info)
{
    vmem_aux_free(&serviceInfo_maux, svc_info);
    return ;   
}

int vserviceInfo_init(vserviceInfo* svc_info, int what, struct sockaddr_in* addr)
{
    vassert(svc_info);
    vassert(addr);

    vserviceId_make(&svc_info->id);
    vsockaddr_copy(&svc_info->addr, addr);
    svc_info->usage = what;
    return 0;
}

void vserviceInfo_dump(vserviceInfo* svc_info)
{
    vassert(svc_info);
    vnodeId_dump(&svc_info->id);
    vsockaddr_dump(&svc_info->addr);
    vdump(printf("what:%d", svc_info->usage));

    return ;
}

