#include <getopt.h>
#include "vglobal.h"

static int daemonize  = 0;
static int logstdout  = 0;
static int defcfgfile = 1;
static char cfgfile[1024];

static int show_help = 0;
static int show_ver  = 0;

static
struct option long_options[] = {
    {"daemon",      no_argument,        &daemonize, 1},
    {"iteractive",  no_argument,        &daemonize, 0},
    {"help",        no_argument,        &show_help, 1},
    {"version",     no_argument,        &show_ver,  1},
    {"log-out",     no_argument,        &logstdout, 1},
    {"conf-file",   required_argument,  0,        'f'},
    {0, 0, 0, 0}
};

void show_usage(void)
{
    printf("Usage: vdhtd [OPTION...]\n");
    printf("  -D, --daemon                  Become a daemon.\n");
    printf("  -i, --interactive             Run iteractive (not a daemon)\n");
    printf("  -S, --log-stdout              Log out stdout.\n");
    printf("  -f  --conf-file=CONFIGFILE    Use alternate configuration file.\n");
    printf("\n");
    printf("Help options\n");
    printf("  -h  --help                    Show this help message.\n");
    printf("  -v, --version                 Print version.\n");
    printf("\n");
    return ;
}

void show_version(void)
{
    printf("Version 0.0.1\n");
    return ;
}

int main(int argc, char** argv)
{
    char* cfg_file = NULL;
    int opt_idx = 0;
    int ret = 0;
    int c = 0;

    while(c >= 0) {
        c = getopt_long(argc, argv, "DiSs:f:hv", long_options, &opt_idx);
        if (c < 0) {
            break;
        }
        switch(c) {
        case 0: {
            if (long_options[opt_idx].flag != 0)
                break;
            printf ("option %s", long_options[opt_idx].name);
            if (optarg)
                printf (" with arg %s", optarg);
            printf ("\n");
            break;
        }
        case 'D':
            daemonize = 1;
            break;
        case 'i':
            daemonize = 0;
            break;
        case 'S':
            logstdout = 1;
            break;
        case 'f':
            defcfgfile = 0;
            if (strlen(optarg) + 1 >= 1024) {
                printf("Too long path for config file (less than 1K).\n");
                exit(-1);
            }
            strcpy(cfgfile, optarg);
            break;
        case 'h':
            show_help = 1;
            break;
        case 'v':
            show_ver = 1;
            break;
        default:
            printf("Invalid option -%c: Unknown option.\n", (char)c);
            show_usage();
            exit(-1);
        }
    }
    if (optind < argc) {
        printf("Too many arguments.\n");
        show_usage();
        exit(-1);
    }

    if (show_help) {
        show_usage();
        exit(0);
    }
    if (show_ver) {
        show_version();
        exit(0);
    }

    if (daemonize) {
        int pid = fork();
        if (pid < 0) {
            perror("fork: ");
            exit(-1);
        }else if (pid > 0) {
            exit(0);
        }else {
            setsid();
        }
    }

    {
        struct vappmain app;
        if (defcfgfile) {
            cfg_file = "vdht.conf";
        } else {
            cfg_file = cfgfile;
        }

        ret = vappmain_init(&app, cfg_file, logstdout);
        if (ret < 0) {
            printf("failed to initialize appmain\n");
            exit(-1);
        }
        ret = app.ops->run(&app);
        if (ret < 0) {
            printf("failed to start running appmain\n");
            vappmain_deinit(&app);
            exit(-1);
        }
        vappmain_deinit(&app);
    }
    return 0;
}

