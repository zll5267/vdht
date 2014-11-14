#include "vglobal.h"
#include "vcfg.h"

enum {
    CFG_INT = 0,
    CFG_STR,
    CFG_UNKOWN
};

struct vcfg_item {
    struct vlist list;
    int   type;
    char* key;
    char* val;
    int   nval;
};

static MEM_AUX_INIT(cfg_item_cache, sizeof(struct vcfg_item), 8);
static
struct vcfg_item* vitem_alloc(void)
{
    struct vcfg_item* item = NULL;

    item = (struct vcfg_item*)vmem_aux_alloc(&cfg_item_cache);
    vlog((!item), elog_vmem_aux_alloc);
    retE_p((!item));
    return item;
}

static
void vitem_free(struct vcfg_item* item)
{
    vassert(item);
    if (item->key) {
        free(item->key);
    }
    if (item->val) {
        free(item->val);
    }
    vmem_aux_free(&cfg_item_cache, item);
    return ;
}

static
void vitem_init_int(struct vcfg_item* item, char* key, int nval)
{
    vassert(item);
    vassert(key);

    vlist_init(&item->list);
    item->type = CFG_INT;
    item->key  = key;
    item->val  = NULL;
    item->nval = nval;
    return ;
}

static
void vitem_init_str(struct vcfg_item* item, char* key, char* val)
{
    vassert(item);
    vassert(key);
    vassert(val);

    vlist_init(&item->list);
    item->type = CFG_STR;
    item->key  = key;
    item->val  = val;
    item->nval = 0;
    return ;
}

/*
 * strip underlines '_' from head and tail of string.
 */
static
int _strip_underline(char* str)
{
    char* cur = str;
    vassert(str);

    // strip '_' from the head of str.
    while(*cur == '_') {
        char* tmp = cur;
        while(*tmp != '\0') {
            *tmp = *(tmp + 1);
            tmp++;
        }
    }
    // strip '_' from the tail of str.
    cur += strlen(str) -1;
    while(*cur == '_') {
        *cur = '\0';
        cur--;
    }
    return 0;
}

static char last_section[64];
static
int eat_section_ln(struct vconfig* cfg, char* section_ln)
{
    char* cur = section_ln;
    int section_aten = 0;

    vassert(cfg);
    vassert(section_ln);

    cur++; //skip '['
    while (*cur != '\n') {
        switch(*cur) {
        case ' ':
        case '\t':
            retE((!section_aten));
            cur++;
            break;
        case ']': {
            retE((section_aten));
            memset(last_section, 0, 64);
            strncpy(last_section, section_ln + 1, cur-section_ln - 1);
            section_aten = 1;
            cur++;
            break;
        }
        case '0'...'9':
        case 'a'...'z':
        case 'A'...'Z':
            retE((section_aten));
            cur++;
            break;
        default:
            retE((1));
        }
    }
    cur++; // skip '\n'
    return (cur - section_ln);
}

static
int eat_comment_ln(struct vconfig* cfg, char* comm_ln)
{
    char* cur = (char*)comm_ln;

    vassert(cfg);
    vassert(comm_ln);

    cur++; // skip ';'
    while(*cur != '\n') cur++;
    cur++;

    return (cur - comm_ln);
}

static
int eat_param_ln(struct vconfig* cfg, char* param_ln)
{
    struct vcfg_item* item = NULL;
    char* cur = (char*)param_ln;
    char* val_pos = NULL;
    int key_aten = 0;
    int is_int   = 1;
    char* key = NULL;
    char* val = NULL;
    int  nval = 0;

    vassert(cfg);
    vassert(param_ln);

    while(*cur != '\n') {
        switch(*cur) {
        case '0'...'9':
            cur++;
            break;
        case 'a'...'z':
        case 'A'...'Z':
        case '.':
        case '/':
        case '_':
            cur++;
            if (key_aten) {
                is_int = 0;
            }
            break;
        case ' ':    //change whitepsace and tab to be '_'.
        case '\t':
            *cur = '_';
            cur++;
            retE((*cur == ' ' || *cur == '\t'));
            break;
        case '=': {
            int sz = 0;
            retE(key_aten);

            sz = cur - param_ln;
            sz += 1; // for '\0'
            sz += strlen(last_section);
            sz += 1; // for '.'

            key = malloc(sz);
            vlog((!key), elog_malloc);
            retE((!key));
            memset(key, 0, sz);

            strcpy(key, last_section);
            strcat(key, ".");
            strncat(key, param_ln, cur-param_ln);
            _strip_underline(key);

            key_aten = 1;
            val_pos = ++cur;
            break;
        }
        default:
            retE(1);
            break;
        }
    }

    { // meet line feed '\n'.
      // deal with value.
        char tmp[32];
        memset(tmp, 0, 32);
        strncpy(tmp, val_pos, cur - val_pos);
        _strip_underline(tmp);

        if (is_int) {
            errno = 0;
            nval = strtol(tmp, NULL, 10);
            retE((errno));
        } else {
            val = malloc(strlen(tmp) + 1);
            vlog((!val), elog_malloc);
            ret1E((!val), free(key));
            strcpy(val, tmp);
        }
    }
    cur++; //skip '\n'

    item = vitem_alloc();
    ret2E((!item), free(key), free(val));

    if (is_int) {
        vitem_init_int(item, key, nval);
    } else {
        vitem_init_str(item, key, val);
    }
    vlist_add_tail(&cfg->items, &item->list);
    return (cur - param_ln);
}

static
int _aux_parse(struct vconfig* cfg, void* buf)
{
    char* cur = (char*)buf;
    int ret = 0;

    vassert(cfg);
    vassert(buf);

    while(*cur != '\0') {
        switch(*cur) {
        case ' ':
        case '\t':
        case '\n':
        case '/':
            cur++;
            break;
        case '[':
            ret = eat_section_ln(cfg, cur);
            retE((ret < 0));
            cur += ret;
            break;
        case ';':
            ret = eat_comment_ln(cfg, cur);
            retE((ret < 0));
            cur += ret;
            break;
        case 'a' ... 'z':
        case 'A' ... 'Z':
        case '0' ... '9':
            ret = eat_param_ln(cfg, cur);
            retE((ret < 0));
            cur += ret;
            break;
        default:
            retE((1));
        }
    }
    return 0;
}

/*
 * the routine to load all config items by parsing config file.
 * @cfg:
 * @filename
 */
static
int _vcfg_parse(struct vconfig* cfg, const char* filename)
{
    struct stat stat;
    char* buf = NULL;
    int fd  = 0;
    int ret = 0;

    vassert(cfg);
    vassert(filename);

    fd = open(filename, O_RDONLY);
    vlog((fd < 0), elog_open);
    retE((fd < 0));

    memset(&stat, 0, sizeof(stat));
    ret = fstat(fd, &stat);
    vlog((ret < 0), elog_fstat);
    ret1E((ret < 0), close(fd));

    buf = malloc(stat.st_size + 1);
    vlog((!buf), elog_malloc);
    ret1E((!buf), close(fd));

    ret = read(fd, buf, stat.st_size);
    vlog((!buf), elog_read);
    vcall_cond((!buf), close(fd));
    vcall_cond((!buf), free(buf));
    retE((!buf));

    buf[stat.st_size] = '\0';
    close(fd);

    vlock_enter(&cfg->lock);
    ret = _aux_parse(cfg, buf);
    vlock_leave(&cfg->lock);
    retE((ret < 0));
    return 0;
}

/*
 * the routine to dump all config items.
 * @cfg:
 */
static
void _vcfg_dump(struct vconfig* cfg)
{
    struct vcfg_item* item = NULL;
    struct vlist* node = NULL;
    vassert(cfg);

    vdump(printf("-> CONFIG"));
    vlock_enter(&cfg->lock);
    __vlist_for_each(node, &cfg->items) {
        item = vlist_entry(node, struct vcfg_item, list);
        switch(item->type) {
        case CFG_INT:
            vdump(printf("[%s]:%d", item->key, item->nval));
            break;
        case CFG_STR:
            vdump(printf("[%s]:%s", item->key, item->val));
            break;
        default:
            vassert(0);
            break;
        }
    }
    vlock_leave(&cfg->lock);
    vdump(printf("<- CONFIG"));
    return ;
}

/*
 * the routine to clear all config items
 * @cfg:
 */
static
int _vcfg_clear(struct vconfig* cfg)
{
    struct vcfg_item* item = NULL;
    struct vlist* node = NULL;
    vassert(cfg);

    vlock_enter(&cfg->lock);
    while(!vlist_is_empty(&cfg->items)) {
        node = vlist_pop_head(&cfg->items);
        item = vlist_entry(node, struct vcfg_item, list);
        vitem_free(item);
    }
    vlock_leave(&cfg->lock);
    return 0;
}

/*
 * the routine to get value of config item by given kye @key.
 * @cfg:
 * @key:
 * @value:
 */
static
int _vcfg_get_int(struct vconfig* cfg, const char* key, int* value)
{
    struct vcfg_item* item = NULL;
    struct vlist* node = NULL;
    int ret = -1;

    vassert(cfg);
    vassert(key);
    vassert(value);

    vlock_enter(&cfg->lock);
    __vlist_for_each(node, &cfg->items) {
        item = vlist_entry(node, struct vcfg_item, list);
        if ((!strcmp(item->key, key)) && (item->type == CFG_INT)) {
            *value = item->nval;
            ret = 0;
            break;
        }
    }
    vlock_leave(&cfg->lock);
    return ret;
}

/*
 * the routine to get value of config item by given kye @key.
 * @cfg:
 * @key:
 * @value:
 * @def_value:
 */
static
int _vcfg_get_int_ext(struct vconfig* cfg, const char* key, int* value, int def_value)
{
    struct vcfg_item* item = NULL;
    char* kay = NULL;
    int ret = 0;

    vassert(cfg);
    vassert(key);
    vassert(value);

    ret = cfg->ops->get_int(cfg, key, value);
    retS((ret >= 0));

    kay = (char*)malloc(strlen(key) + 1);
    retE((!kay));
    strcpy(kay, key);

    item = vitem_alloc();
    ret1E((!item), free(kay));
    vlist_init(&item->list);
    item->key = kay;
    item->val = NULL;
    item->nval = def_value;
    item->type = CFG_INT;

    vlock_enter(&cfg->lock);
    vlist_add_tail(&cfg->items, &item->list);
    vlock_leave(&cfg->lock);

    *value = def_value;
    return 0;
}

/*
 * the routine to get value of config item by given kye @key.
 * @cfg:
 * @key:
 * @value:
 * @sz:
 */
static
int _vcfg_get_str(struct vconfig* cfg, const char* key, char* value, int sz)
{
    struct vcfg_item* item = NULL;
    struct vlist* node = NULL;
    int ret = -1;

    vassert(cfg);
    vassert(key);
    vassert(value);
    vassert(sz > 0);

    vlock_enter(&cfg->lock);
    __vlist_for_each(node, &cfg->items) {
        item = vlist_entry(node, struct vcfg_item, list);
        if ((!strcmp(item->key, key))
             && (item->type == CFG_STR)
             && (strlen(item->val) < sz)) {
            strcpy(value, item->val);
            ret = 0;
            break;
        }
    }
    vlock_leave(&cfg->lock);
    return ret;
}

/*
 * the routine to get value of config item by given kye @key.
 * @cfg:
 * @key:
 * @value:
 * @sz:
 * @def_value:
 */
static
int _vcfg_get_str_ext(struct vconfig* cfg, const char* key, char* value, int sz, char* def_value)
{
    struct vcfg_item* item = NULL;
    char* val = NULL;
    char* kay = NULL;
    int ret = 0;

    vassert(cfg);
    vassert(key);
    vassert(value);
    vassert(sz > 0);
    vassert(def_value);

    ret = cfg->ops->get_str(cfg, key, value, sz);
    retS((ret >= 0));
    retE((sz < strlen(def_value) + 1));

    val = (char*)malloc(strlen(def_value) + 1);
    retE((!val));
    strcpy(val, def_value);
    kay  = (char*)malloc(strlen(key) + 1);
    ret1E((!kay), free(val));
    strcpy(kay, key);

    item = vitem_alloc();
    ret2E((!item), free(val), free(kay));
    vlist_init(&item->list);
    item->key  = kay;
    item->val  = val;
    item->type = CFG_STR;
    item->nval = 0;

    vlock_enter(&cfg->lock);
    vlist_add_tail(&cfg->items, &item->list);
    vlock_leave(&cfg->lock);

    strcpy(value, def_value);
    return 0;
}

static
struct vconfig_ops cfg_ops = {
    .parse       = _vcfg_parse,
    .clear       = _vcfg_clear,
    .dump        = _vcfg_dump,
    .get_int     = _vcfg_get_int,
    .get_int_ext = _vcfg_get_int_ext,
    .get_str     = _vcfg_get_str,
    .get_str_ext = _vcfg_get_str_ext
};

int vconfig_init(struct vconfig* cfg)
{
    vassert(cfg);

    vlist_init(&cfg->items);
    vlock_init(&cfg->lock);
    cfg->ops = &cfg_ops;
    return 0;
}

void vconfig_deinit(struct vconfig* cfg)
{
    vassert(cfg);

    cfg->ops->clear(cfg);
    vlock_deinit(&cfg->lock);
    return ;
}

