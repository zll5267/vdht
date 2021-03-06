#include "vglobal.h"
#include "vdht_core.h"

static MEM_AUX_INIT(be_cache, sizeof(struct be_node), 16);
struct be_node* be_alloc(int type)
{
    struct be_node* node = NULL;
    vassert(type >= BE_STR);
    vassert(type <  BE_BUT);

    node = (struct be_node*)vmem_aux_alloc(&be_cache);
    vlogEv((!node), elog_vmem_aux_alloc);
    retE_p((!node));

    memset(node, 0, sizeof(*node));
    node->type = type;
    return node;
}

void be_free(struct be_node* node)
{
    vassert(node);
    switch(node->type) {
    case BE_STR:
        free(unoff_addr(node->val.s, sizeof(int32_t)));
        break;
    case BE_INT:
        break;
    case BE_LIST: {
        int i = 0;

        for (; node->val.l[i]; ++i) {
            be_free(node->val.l[i]);
        }
        free(node->val.l);
        break;
    }
    case BE_DICT: {
        int i = 0;
        for (; node->val.d[i].val; ++i) {
            free(unoff_addr(node->val.d[i].key, sizeof(int32_t)));
            be_free(node->val.d[i].val);
        }
        free(node->val.d);
        break;
    }
    default:
        vassert(0);
    }
    vmem_aux_free(&be_cache, node);
}

static
int _be_decode_int(char** data, int* data_len, int32_t* int_val)
{
    char *endp = NULL;

    errno = 0;
    *int_val = (int32_t)strtol(*data, &endp, 10);
    retE((errno));
    *data_len -= (endp - *data);
    *data = endp;
    return 0;
}

static
char* _be_decode_str(char** data, int* data_len)
{
    char* s = NULL;
    int32_t len = 0;
    int ret = 0;

    ret = _be_decode_int(data, data_len, &len);
    retE_p((ret < 0));
    retE_p((len > *data_len - 1));

    if (**data == ':') {
        *data += 1;
        *data_len -= 1;

        s = (char*)malloc(sizeof(len) + len + 1);
        vlogEv((!s), elog_malloc);
        retE_p((!s));
        memset(s, 0, sizeof(len) + len + 1);

        set_int32(s, len);
        s = offset_addr(s, sizeof(int32_t));
        strncpy(s, *data, len);

        *data += len;
        *data_len -= len;
    }
    return s;
}

static
struct be_node *_be_decode(char **data, int *data_len)
{
    struct be_node* node = NULL;

    vassert(*data);
    vassert(data_len);
    vassert(*data_len > 0);

    switch (**data) {
    case 'l': { //list
        int i = 0;
        node = be_alloc(BE_LIST);
        vlogEv((!node), elog_be_alloc);
        retE_p((!node));

        --(*data_len);
        ++(*data);
        while (**data != 'e') {
            struct be_node** l = NULL;
            l = (struct be_node**)realloc(node->val.l, (i+2)*sizeof(struct be_node**));
            vlogEv((!l), elog_realloc);
            ret1E_p((!l), be_free(node));
            node->val.l = l;

            node->val.l[i] = _be_decode(data, data_len);
            ret1E_p((!node->val.l[i]), be_free(node));
            ++i;
        }
        --(*data_len);
        ++(*data);

        /* empty list case. */
        if (i == 0){
            struct be_node** l = NULL;
            l = (struct be_node**)realloc(node->val.l, sizeof(struct be_node**));
            vlogEv((!l), elog_realloc);
            ret1E_p((!l), be_free(node));
            node->val.l = l;
        }
        node->val.l[i] = NULL;
        break;
    }
    case 'd': { /* dictionaries */
        int i = 0;
        node = be_alloc(BE_DICT);
        vlogEv((!node), elog_be_alloc);
        retE_p((!node));

        --(*data_len);
        ++(*data);
        while(**data != 'e') {
            struct be_dict* d = NULL;
            d = (struct be_dict*)realloc(node->val.d, (i+2)*sizeof(struct be_dict));
            vlogEv((!d), elog_realloc);
            ret1E_p((!d), be_free(node));
            node->val.d = d;

            node->val.d[i].key = _be_decode_str(data, data_len);
            ret1E_p((!node->val.d[i].key), be_free(node));
            node->val.d[i].val = _be_decode(data, data_len);
            ret1E_p((!node->val.d[i].val), be_free(node));
            ++i;
        }
        --(*data_len);
        ++(*data);

        if (i == 0) {
            struct be_dict* d = NULL;
            d = (struct be_dict*)realloc(node->val.d, sizeof(struct be_dict));
            vlogEv((!d), elog_realloc);
            ret1E_p((!d), be_free(node));
            node->val.d = d;
        }
        node->val.d[i].val = NULL;
        break;
    }
    case 'i': { /* integers */
        int ret = 0;
        node = be_alloc(BE_INT);
        vlogEv((!node), elog_be_alloc);
        retE_p((!node));

        --(*data_len);
        ++(*data);
        ret = _be_decode_int(data, data_len, &node->val.i);
        ret1E_p((ret < 0), be_free(node));
        ret1E_p((**data != 'e'), be_free(node));
        --(*data_len);
        ++(*data);
        break;
    }
    case '0'...'9': { /* byte strings */
        node = be_alloc(BE_STR);
        vlogEv((!node), elog_be_alloc);
        retE_p((!node));

        node->val.s = _be_decode_str(data, data_len);
        ret1E_p((!node->val.s), be_free(node));
        break;
    }
    default: /* invalid*/
        break;
    }
    return node;
}

struct be_node *be_decode(char *data, int len)
{
    return _be_decode(&data, &len);
}

struct be_node *be_decode_str(char *data)
{
    return be_decode(data, strlen(data));
}

struct be_node *be_create_dict(void)
{
    return be_decode_str("de");
}

struct be_node *be_create_list(void)
{
    return be_decode_str("le");
}

struct be_node* be_create_str(char* str)
{
    struct be_node* node = NULL;
    char* s = NULL;
    int len = strlen(str);
    vassert(str);

    node = be_alloc(BE_STR);
    vlogEv((!node), elog_be_alloc);
    retE_p((!node));

    s = (char*)malloc(sizeof(int32_t) + len + 1);
    vlogEv((!s), elog_malloc);
    ret1E_p((!s), be_free(node));

    set_int32(s, len);
    s = offset_addr(s, sizeof(int32_t));
    strcpy(s, str);

    node->val.s = s;
    return node;
}

struct be_node* be_create_int(int num)
{
    struct be_node* node = NULL;

    node = be_alloc(BE_INT);
    vlogEv((!node), elog_be_alloc);
    retE_p((!node));

    node->val.i = num;
    return node;
}

struct be_node* be_create_vtoken(vtoken* token)
{
    struct be_node* node = NULL;
    char buf[64];
    vassert(token);

    memset(buf, 0, 64);
    vtoken_strlize(token, buf, 64);
    node = be_create_str(buf);
    retE_p((!node));
    return node;
}

struct be_node* be_create_addr(struct sockaddr_in* addr)
{
    struct be_node* node = NULL;
    char buf[64];
    vassert(addr);

    memset(buf, 0, 64);
    vsockaddr_strlize(addr, buf, 64);
    node = be_create_str(buf);
    retE_p((!node));
    return node;
}

struct be_node* be_create_ver(vnodeVer* ver)
{
    struct be_node* node = NULL;
    char buf[64];
    vassert(ver);

    memset(buf, 0, 64);
    vnodeVer_strlize(ver, buf, 64);
    node = be_create_str(buf);
    retE_p((!node));
    return node;
}

int be_add_keypair(struct be_node *dict, char *str, struct be_node *node)
{
    struct be_dict* d = NULL;
    int len = strlen(str);
    char* s = NULL;
    int i = 0;

    vassert(dict);
    vassert(str);
    vassert(dict->type == BE_DICT);

    s = (char*)malloc(sizeof(int32_t) + len + 1);
    vlogEv((!s), elog_malloc);
    retE((!s));

    set_int32(s, len);
    s = offset_addr(s, sizeof(int32_t));
    memcpy(s, str, len);
    s[len] = '\0';

    for (; dict->val.d[i].val; i++);
    d = (struct be_dict*)realloc(dict->val.d, (i+2)*sizeof(*d));
    vlogEv((!d), elog_realloc);
    ret1E((!d), free(unoff_addr(s, sizeof(int32_t))));

    dict->val.d = d;
    dict->val.d[i].key = s;
    dict->val.d[i].val = node;
    i++;
    dict->val.d[i].val = NULL;
    return 0;
}

int be_add_list(struct be_node *list, struct be_node *node)
{
    struct be_node** l = NULL;
    int i = 0;

    vassert(list);
    vassert(node);
    vassert(list->type == BE_LIST);

    for (; list->val.l[i]; i++);
    l = (struct be_node**)realloc(list->val.l, (i + 2)*sizeof(struct be_node**));
    vlogEv((!l), elog_realloc);
    retE((!l));
    list->val.l = l;

    list->val.l[i] = node;
    i++;
    list->val.l[i] = NULL;
    return 0;
}

int be_encode(struct be_node *node, char *buf, int len)
{
    int off = 0;
    int ret = 0;

    vassert(node);
    vassert(buf);
    vassert(len > 0);

    switch(node->type) {
    case BE_STR: {
        int _len = get_int32(unoff_addr(node->val.s, sizeof(int32_t)));
        ret = snprintf(buf+off, len-off, "%i:", _len);
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        ret = snprintf(buf+off, len-off, "%s", node->val.s);
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        break;
    }
    case BE_INT:
        ret = snprintf(buf+off, len-off, "i%ie", node->val.i);
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        break;
    case BE_LIST: {
        int i = 0;
        ret = snprintf(buf+off, len-off, "l");
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        for (; node->val.l[i]; i++) {
            ret = be_encode(node->val.l[i], buf+off, len-off);
            retE((ret < 0));
            off += ret;
        }
        ret = snprintf(buf+off, len-off, "e");
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        break;
    }
    case BE_DICT: {
        int i = 0;
        ret = snprintf(buf+off, len-off, "d");
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        for (i = 0; node->val.d[i].val; i++) {
            char* _key = node->val.d[i].key;
            int   _len = get_int32(unoff_addr(_key, sizeof(int32_t)));
            ret = snprintf(buf + off, len - off, "%i:%s", _len, _key);
            vlogEv((ret >= len-off), elog_snprintf);
            retE((ret >= len-off));
            off += ret;

            ret = be_encode(node->val.d[i].val, buf+off, len-off);
            retE((ret < 0));
            off += ret;
        }
        ret = snprintf(buf+off, len-off, "e");
        vlogEv((ret >= len-off), elog_snprintf);
        retE((ret >= len-off));
        off += ret;
        break;
    }
    default:
        return -1;
        break;
    }
    return off;
}

int be_unpack_int(struct be_node* node, int* val)
{
    vassert(node);
    vassert(val);

    retE((BE_INT != node->type));
    *val = node->val.i;
    return 0;
}

int be_unpack_token(struct be_node* node, vtoken* token)
{
    char* s = NULL;
    int ret = 0;

    vassert(node);
    vassert(token);

    retE((BE_STR != node->type));

    s = unoff_addr(node->val.s, sizeof(int32_t));
    ret = get_int32(s);
    retE((ret != strlen(node->val.s)));

    ret = vtoken_unstrlize(node->val.s, token);
    retE((ret < 0));
    return 0;
}

int be_unpack_ver(struct be_node* node, vnodeVer* ver)
{
    char* s = NULL;
    int ret = 0;

    vassert(node);
    vassert(ver);

    retE((BE_STR != node->type));

    s = unoff_addr(node->val.s, sizeof(int32_t));
    ret = get_int32(s);
    retE((ret != strlen(node->val.s)));

    ret = vnodeVer_unstrlize(node->val.s, ver);
    retE((ret < 0));
    return 0;
}

int be_unpack_addr(struct be_node* node, struct sockaddr_in* addr)
{
    char* s = NULL;
    int ret = 0;

    vassert(node);
    vassert(addr);

    retE((BE_STR != node->type));

    s = unoff_addr(node->val.s, sizeof(int32_t));
    ret = get_int32(s);
    retE((ret != strlen(node->val.s)));

    ret = vsockaddr_unstrlize(node->val.s, addr);
    retE((ret < 0));
    return 0;
}

int be_node_by_key(struct be_node* dict, char* key, struct be_node** node)
{
    int i = 0;
    vassert(dict);
    vassert(key);
    vassert(node);

    retE((BE_DICT != dict->type));
    for(; dict->val.d[i].val; i++) {
        if (!strcmp(key, dict->val.d[i].key)) {
            *node = dict->val.d[i].val;
            return 0;
        }
    }
    return -1;
}

int be_node_by_2keys(struct be_node* dict, char* key1, char* key2, struct be_node** node)
{
    struct be_node* tmp = NULL;
    int ret = 0;
    vassert(dict);
    vassert(key1);
    vassert(key2);
    vassert(node);

    ret = be_node_by_key(dict, key1, &tmp);
    if ((ret < 0) || (BE_DICT != tmp->type)) {
        return -1;
    }
    ret = be_node_by_key(tmp, key2, node);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

