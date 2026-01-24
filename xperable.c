// 元のコード https://github.com/j4nn/xperable

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

void fbusb_exit(struct fbusb *dev)
{
    libusb_release_interface(dev->h, dev->iface);
    libusb_close(dev->h);
    libusb_exit(NULL);
    free(dev);
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
        printf("fbusb_bufcmd_resp recv unknown fastboot response: '%c%c%c%c' (rspsz=0x%04x received=0x%04x)\n", s[0], s[1], s[2], s[3], *rspsz, received);
        *rspsz = received;
        return FASTBOOT_UNKNOWN;
    }
    if (received < 0)
    {
        printf("fbusb_bufcmd_resp recv failed (rspsz=0x%04x)\n", *rspsz);
        *rspsz = 0;
        return received;
    }
    printf("fbusb_bufcmd_resp recv invalid fastboot response: received=0x%04x (rspsz=0x%04x)\n", received, *rspsz);
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
            printf("fbusb_bufcmd send incomplete: reqsz=0x%02x res=0x%04x\n", reqsz, res);
            return -1;
        }
        printf("fbusb_bufcmd send failed: reqsz=0x%02x res=0x%04x\n", reqsz, res);
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

static unsigned char rxbuff[1024 * 1024 * 64];

// ヘッダー

struct fbusb *fbusb_init(int vid, int pid, int iface, int epi, int epo);
static int getvar_all(struct fbusb *dev);

// e ヘッダー

int main(){

    printf("開始");

    const int vendor_id = 0x0fce, product_id = 0x0dde, inter_face = 0, endpoint_in = 0x81, endpoint_out = 0x01;
    struct fbusb *dev = fbusb_init(vendor_id, product_id, inter_face, endpoint_in, endpoint_out);
    if(dev == NULL){ return 1; }
    getvar_all(dev);
    fbusb_exit(dev);
    return 0;
}

struct fbusb *fbusb_init(int vid, int pid, int iface, int epi, int epo){
    int res;
    struct fbusb *dev;
    libusb_device_handle *h;

    res = libusb_init(NULL);
    if (res < 0)
    {
        printf("libusb_init failed: %s\n", libusb_strerror(res));
        return NULL;
    }

    // | 端末の接続を待機中... | waiting for device connection. |

    h = libusb_open_device_with_vid_pid(NULL, vid, pid);
    if (h == NULL)
    {
        printf("libusb_open_device_with_vid_pid (%04x:%04x) failed\n", vid, pid);
        goto err_exit1;
    }

    if (libusb_kernel_driver_active(h, 0) == 1)
    {
        if (libusb_detach_kernel_driver(h, 0) != 0)
        {
            printf("libusb_detach_kernel_driver failed\n");
            goto err_exit2;
        }
    }

    res = libusb_claim_interface(h, iface);
    if (res < 0)
    {
        printf("libusb_claim_interface failed: %s\n", libusb_strerror(res));
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

static int getvar_all(struct fbusb *dev){
    int res = fbusb_strcmd(dev, "getvar:all", rxbuff, 65);
    while (res == FASTBOOT_INFO)
    {
        res = strncmp(rxbuff, "version-bootloader:", 19) == 0 ? 1 : 0;
        if(res == 1){
            printf("%s\n", rxbuff);
            res = fbusb_strcmd_resp(dev, rxbuff, 65);
        }
    }
    if (res != FASTBOOT_OKAY)
    {
        if (res > 0)
        {
            printf("getvar all failed: %s\n", rxbuff);
        }
        else
        {
            printf("getvar all protocol error, res=%d\n", res);
        }
    }
    return res;
}
