// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * xperable.c - Xperia ABL fastboot Exploit of CVE-2021-1931
 *
 * Copyright (C) 2025 j4nn at xdaforums
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <errno.h>

#include <libusb-1.0/libusb.h>

// "logging.h" //

enum
{
    LOG_ERR = 1,
    LOG_NFO,
    LOG_DBG,
};

#define eprintf(lev, fmt, args...)        \
    do                                    \
    {                                     \
        if (verbosity >= lev)             \
            fprintf(stderr, fmt, ##args); \
    } while (0)

#define oprintf(lev, fmt, args...)        \
    do                                    \
    {                                     \
        if (verbosity >= lev)             \
            fprintf(stdout, fmt, ##args); \
    } while (0)

#define PERR(fmt, args...) eprintf(LOG_ERR, "[!] " fmt, ##args)
#define PNFO(fmt, args...) eprintf(LOG_NFO, "[+] " fmt, ##args)
#define PDBG(fmt, args...) eprintf(LOG_DBG, "[.] " fmt, ##args)

#define PNFO_CONT(fmt, args...) eprintf(LOG_NFO, fmt, ##args)
#define PDBG_CONT(fmt, args...) eprintf(LOG_DBG, fmt, ##args)

#define POUT(fmt, args...) oprintf(LOG_NFO, fmt, ##args)

// e "logging.h" //

// "fbusb.h" //

struct fbusb;

enum
{
    FASTBOOT_OKAY,
    FASTBOOT_FAIL,
    FASTBOOT_DATA,
    FASTBOOT_INFO,
    FASTBOOT_UNKNOWN
};

static const char *fastboot_results[] = {
    [FASTBOOT_OKAY] = "OKAY",
    [FASTBOOT_FAIL] = "FAIL",
    [FASTBOOT_DATA] = "DATA",
    [FASTBOOT_INFO] = "INFO",
    [FASTBOOT_UNKNOWN] = "UNKN",
};

struct fbusb
{
    libusb_device_handle *h;
    int iface;
    int epi;
    int epo;
    int maxsize;
    int timeout;
};

int fastboot_parse_result(const char *status)
{
    int i;
    for (i = 0; i < FASTBOOT_UNKNOWN; i++)
        if (strncmp(status, fastboot_results[i], 4) == 0)
            return i;
    return i;
}

struct fbusb *fbusb_init(int vid, int pid, int iface, int epi, int epo)
{
    int res;
    struct fbusb *dev;
    libusb_device_handle *h;

    res = libusb_init(NULL);
    if (res < 0)
    {
        PERR("libusb_init failed: %s\n", libusb_strerror(res));
        return NULL;
    }

    h = libusb_open_device_with_vid_pid(NULL, vid, pid);
    if (h == NULL)
    {
        PERR("libusb_open_device_with_vid_pid (%04x:%04x) failed\n",
             vid, pid);
        goto err_exit1;
    }

    if (libusb_kernel_driver_active(h, 0) == 1)
    {
        if (libusb_detach_kernel_driver(h, 0) != 0)
        {
            PERR("libusb_detach_kernel_driver failed\n");
            goto err_exit2;
        }
    }

    res = libusb_claim_interface(h, iface);
    if (res < 0)
    {
        PERR("libusb_claim_interface failed: %s\n", libusb_strerror(res));
        goto err_exit2;
    }

    dev = calloc(1, sizeof(struct fbusb));
    if (dev != NULL)
    {
        dev->h = h;
        dev->iface = iface;
        dev->epi = epi;
        dev->epo = epo;
        dev->maxsize = 16 * 1024 * 1024;
        dev->timeout = 5000;
        return dev;
    }

    libusb_release_interface(h, iface);
err_exit2:
    libusb_close(h);
err_exit1:
    libusb_exit(NULL);
    return NULL;
}

void fbusb_exit(struct fbusb *dev)
{
    libusb_release_interface(dev->h, dev->iface);
    libusb_close(dev->h);
    libusb_exit(NULL);
    free(dev);
}

static void fbusb_log(struct fbusb *dev, int ep, void *buff, int len, int done, int res)
{
    int i;
    const char *dir;
    const char *status;
    const unsigned char *ptr = buff;

    dir = (ep & 0x80) ? "<-" : "->";

    switch (res)
    {
    case LIBUSB_SUCCESS:
        status = "OK";
        break;
    case LIBUSB_ERROR_IO:
        status = "IO";
        break;
    case LIBUSB_ERROR_INVALID_PARAM:
        status = "IP";
        break;
    case LIBUSB_ERROR_ACCESS:
        status = "AC";
        break;
    case LIBUSB_ERROR_NO_DEVICE:
        status = "ND";
        break;
    case LIBUSB_ERROR_NOT_FOUND:
        status = "NF";
        break;
    case LIBUSB_ERROR_BUSY:
        status = "BS";
        break;
    case LIBUSB_ERROR_TIMEOUT:
        status = "TO";
        break;
    case LIBUSB_ERROR_OVERFLOW:
        status = "OF";
        break;
    case LIBUSB_ERROR_PIPE:
        status = "PI";
        break;
    case LIBUSB_ERROR_INTERRUPTED:
        status = "IN";
        break;
    case LIBUSB_ERROR_NO_MEM:
        status = "NM";
        break;
    case LIBUSB_ERROR_NOT_SUPPORTED:
        status = "NS";
        break;
    case LIBUSB_ERROR_OTHER:
        status = "OT";
        break;
    default:
        status = "??";
        break;
    }

    PNFO_CONT("      {%08x%s%08x:%s}", len, dir, done, status);

    if (verbosity >= LOG_DBG)
    {
        for (i = 0; i < 16; i++)
            if (i < done)
                PDBG_CONT(" %02x", ptr[i]);
            else
                PDBG_CONT("   ");
    }

    PNFO_CONT(" \"");
    for (i = 0; i < 64 && i < done; i++)
        PNFO_CONT("%c", isprint(ptr[i]) ? ptr[i] : '.');
    PNFO_CONT("\"\n");
}

static int fbusb_transfer(struct fbusb *dev, void *buff, int size, int ep)
{
    int res, idx;
    int len, done;
    int transferred = 0;

    for (idx = 0; idx < size; idx += done)
    {
        len = size - idx;
        if (dev->maxsize > 0)
        {
            if (len > dev->maxsize)
                len = dev->maxsize;
        }

        done = 0;
        res = libusb_bulk_transfer(dev->h, ep, buff + idx, len, &done, dev->timeout);
        transferred += done;
        fbusb_log(dev, ep, buff + idx, len, done, res);

        if (res != 0 && transferred == 0)
        {
            PERR("libusb_bulk_transfer failed: %s ep=0x%02x "
                 "len=0x%04x size=0x%04x\n",
                 libusb_strerror(res), ep, len, size);
            return -1;
        }
        if (done < len)
            break;
    }

    return transferred;
}

int fbusb_send(struct fbusb *dev, void *buff, int size)
{
    return fbusb_transfer(dev, buff, size, dev->epo);
}

int fbusb_recv(struct fbusb *dev, void *buff, int size)
{
    return fbusb_transfer(dev, buff, size, dev->epi);
}

int fbusb_bufcmd_resp(struct fbusb *dev, void *rsp, int *rspsz)
{
    int res;
    int received;
    char *s = rsp;
    if (rspsz == NULL || *rspsz < 4)
        return -1;
    memset(rsp, 0, *rspsz);
    received = fbusb_recv(dev, rsp, *rspsz);
    if (received >= 4)
    {
        res = fastboot_parse_result(rsp);
        if (res < FASTBOOT_UNKNOWN)
        {
            received -= 4;
            memmove(rsp, rsp + 4, received);
            memset(s + received, 0, 4);
            *rspsz = received;
            return res;
        }
        PDBG("fbusb_bufcmd_resp recv unknown fastboot response: "
             "'%c%c%c%c' (rspsz=0x%04x received=0x%04x)\n",
             s[0], s[1], s[2], s[3], *rspsz, received);
        *rspsz = received;
        return FASTBOOT_UNKNOWN;
    }
    if (received < 0)
    {
        PERR("fbusb_bufcmd_resp recv failed (rspsz=0x%04x)\n", *rspsz);
        *rspsz = 0;
        return received;
    }
    PERR("fbusb_bufcmd_resp recv invalid fastboot response: "
         "received=0x%04x (rspsz=0x%04x)\n",
         received, *rspsz);
    *rspsz = received;
    return FASTBOOT_UNKNOWN;
}

int fbusb_bufcmd(struct fbusb *dev, void *req, int reqsz, void *rsp, int *rspsz)
{
    int res = fbusb_send(dev, req, reqsz);
    if (res != reqsz)
    {
        *rspsz = 0;
        if (res > 0)
        {
            PERR("fbusb_bufcmd send incomplete: "
                 "reqsz=0x%02x res=0x%04x\n",
                 reqsz, res);
            return -1;
        }
        PERR("fbusb_bufcmd send failed: reqsz=0x%02x res=0x%04x\n", reqsz, res);
        return -1;
    }
    return fbusb_bufcmd_resp(dev, rsp, rspsz);
}

int fbusb_strcmd(struct fbusb *dev, const char *req, char *rsp, int rspmaxsize)
{
    int res;
    int rspsz = rspmaxsize - 1;
    if (rspsz < 4)
        return -1;
    res = fbusb_bufcmd(dev, (void *)req, strlen(req), rsp, &rspsz);
    if (res >= 0)
        if (rspsz >= 0 && rspsz < rspmaxsize)
            rsp[rspsz] = '\0';
    return res;
}

int fbusb_strcmd_resp(struct fbusb *dev, char *rsp, int rspmaxsize)
{
    int res;
    int rspsz = rspmaxsize - 1;
    if (rspsz < 4)
        return -1;
    res = fbusb_bufcmd_resp(dev, rsp, &rspsz);
    if (res >= 0)
        if (rspsz >= 0 && rspsz < rspmaxsize)
            rsp[rspsz] = '\0';
    return res;
}

// e "fbusb.h" //

typedef void (*testx_patch_t)(unsigned char *buff, int size, int offset);

struct xperable_target
{
    const char *ablver;
    const char *ytname;
    const char *llname;
    int offset;
    int size;
    testx_patch_t setup_test2;
    testx_patch_t setup_test3;
    int64_t test3_hitadj;
    int stage1_cont;
    void (*patch_abl)(unsigned char *ablcode, int extended);
    void (*setup_test4)(unsigned char *buff, int size, int offset,
                        int payloadsize);
    void (*setup_test5)(unsigned char *buff, int size, int offset,
                        int payloadsize);
    const char *test4_cmd;
    int vb_size;
    testx_patch_t test6_patch;
    testx_patch_t test7_patch;
    testx_patch_t test8_patch;
    testx_patch_t test9_patch;
};

#define XPERABLE_TARGET(idstr, blv)          \
    {                                        \
        .ablver = idstr,                     \
        .ytname = #blv,                      \
        .llname = "LinuxLoader-" #blv ".pe", \
        .offset = blv##_offset,              \
        .size = blv##_size,                  \
        .setup_test2 = blv##_setup_test2,    \
        .setup_test3 = blv##_setup_test3,    \
        .test3_hitadj = blv##_test3_hitadj,  \
        .stage1_cont = blv##_stage1_cont,    \
        .patch_abl = blv##_patch_abl,        \
        .setup_test4 = blv##_setup_test4,    \
        .setup_test5 = blv##_setup_test5,    \
        .test4_cmd = blv##_test4_cmd,        \
        .vb_size = blv##_vb_size,            \
        .test6_patch = blv##_test6_patch,    \
        .test7_patch = blv##_test7_patch,    \
        .test8_patch = blv##_test8_patch,    \
        .test9_patch = blv##_test9_patch,    \
    }

#ifdef TARGET_ABL_P118
#include "target-p118.c"
#endif

static struct xperable_target yoshino_abl_targets[] = {
#ifdef TARGET_ABL_P118
    XPERABLE_TARGET("X_Boot_SDM845_LA2.0_P_118", p118),
#endif
    {.ablver = NULL}};

static struct xperable_target *target = &yoshino_abl_targets[0];

static int set_xperable_target(const char *blver)
{
    int i;

    for (i = 0; yoshino_abl_targets[i].ablver != NULL; i++)
        if (strstr(blver, yoshino_abl_targets[i].ablver) != NULL)
        {
            target = &yoshino_abl_targets[i];
            PDBG("Using %s xperable target (offset = 0x%x, size = 0x%x)\n",
                 target->ytname, target->offset, target->size);
            return 0;
        }
    target = &yoshino_abl_targets[i];
    PERR("%s version not supported!\n", blver);
    return -1;
}

int verbosity = LOG_NFO;

static unsigned char rxbuff[1024 * 1024 * 64];

static int getvar_all(struct fbusb *dev)
{
    int res = fbusb_strcmd(dev, "getvar:all", rxbuff, 65);
    while (res == FASTBOOT_INFO)
    {
        res = 0;
        if (strncmp(rxbuff, "version-bootloader:", 19) == 0)
        {
            res = 1;
        }
        if (verbosity >= LOG_NFO && (res == 1 || strncmp(rxbuff, "unlocked:", 9) == 0 || strncmp(rxbuff, "version-baseband:", 17) == 0 || strncmp(rxbuff, "secure:", 7) == 0 || strncmp(rxbuff, "product:", 8) == 0) || verbosity >= LOG_DBG)
        {
            POUT("%s\n", rxbuff);
        }
        if (res)
        {
            set_xperable_target(rxbuff + 19);
        }
        res = fbusb_strcmd_resp(dev, rxbuff, 65);
    }
    if (res != FASTBOOT_OKAY)
    {
        if (res > 0)
        {
            PERR("getvar all failed: %s\n", rxbuff);
        }
        else
        {
            PERR("getvar all protocol error, res=%d\n", res);
        }
    }
    return res;
}

int main()
{
    int vendor_id = 0x0fce, product_id = 0x0dde, inter_face = 0, endpoint_in = 0x81, endpoint_out = 0x01;
    struct fbusb *dev = fbusb_init(vendor_id, product_id, inter_face, endpoint_in, endpoint_out);
    if (dev == NULL)
        return 1;
    getvar_all(dev);
    fbusb_exit(dev);
    return 0;
}
