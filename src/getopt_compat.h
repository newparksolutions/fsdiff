/**
 * @file getopt_compat.h
 * @brief Cross-platform getopt/getopt_long implementation
 *
 * Provides getopt_long for Windows (which lacks it).
 * On Unix, uses system getopt.h.
 */

#ifndef FSDIFF_GETOPT_COMPAT_H
#define FSDIFF_GETOPT_COMPAT_H

#ifdef _WIN32
/* Minimal getopt_long implementation for Windows */

#include <stdio.h>
#include <string.h>

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

static char *optarg = NULL;
static int optind = 1;
static int opterr = 1;
static int optopt = 0;

static int getopt_long(int argc, char *const argv[],
                      const char *optstring,
                      const struct option *longopts,
                      int *longindex) {
    static int sp = 1;
    int c;
    char *cp;

    if (sp == 1) {
        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0') {
            return -1;
        }
        if (strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }

        /* Check for long option */
        if (argv[optind][1] == '-') {
            const char *name = argv[optind] + 2;
            const struct option *opt = longopts;

            while (opt && opt->name) {
                size_t name_len = strlen(opt->name);
                const char *eq = strchr(name, '=');
                size_t cmp_len = eq ? (size_t)(eq - name) : strlen(name);

                if (strncmp(name, opt->name, cmp_len) == 0 && name_len == cmp_len) {
                    optind++;

                    if (opt->has_arg == required_argument) {
                        if (eq) {
                            optarg = (char *)(eq + 1);
                        } else if (optind < argc) {
                            optarg = argv[optind++];
                        } else {
                            if (opterr) {
                                fprintf(stderr, "Option --%s requires an argument\n", opt->name);
                            }
                            return '?';
                        }
                    }

                    if (longindex) *longindex = (int)(opt - longopts);
                    return opt->val;
                }
                opt++;
            }

            if (opterr) {
                fprintf(stderr, "Unknown option: %s\n", argv[optind]);
            }
            optind++;
            return '?';
        }
    }

    /* Short option */
    if ((optopt = c = argv[optind][sp]) == ':' ||
        (cp = strchr(optstring, c)) == NULL) {
        if (opterr && c != ':') {
            fprintf(stderr, "Illegal option: -%c\n", c);
        }
        if (argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
        return '?';
    }

    if (*++cp == ':') {
        if (argv[optind][sp + 1] != '\0') {
            optarg = &argv[optind++][sp + 1];
        } else if (++optind >= argc) {
            if (opterr) {
                fprintf(stderr, "Option -%c requires an argument\n", c);
            }
            sp = 1;
            return '?';
        } else {
            optarg = argv[optind++];
        }
        sp = 1;
    } else {
        if (argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        optarg = NULL;
    }

    return c;
}

#else
/* Unix: use system getopt.h */
#include <getopt.h>
#endif

#endif /* FSDIFF_GETOPT_COMPAT_H */
