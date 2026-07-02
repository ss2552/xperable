// 元のコード https://github.com/j4nn/xperable

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

#define ANDROID_TERMUX true

struct fbusb;

enum {
    LOG_ERR = 1,
    LOG_NFO,
    LOG_DBG,
};

int verbosity = LOG_NFO;
static unsigned char rxbuff[1024 * 1024 * 64];

static void fbusb_log(struct fbusb *dev, int ep, void *buff, int len, int done, int res){
    int i;
    const char *dir;
    const char *status;
    const unsigned char *ptr = buff;

    dir = (ep & 0x80) ? "<-" : "->";

    switch (res){
        case LIBUSB_SUCCESS: status = "OK"; break;
        case LIBUSB_ERROR_IO: status = "IO"; break;
        case LIBUSB_ERROR_INVALID_PARAM: status = "IP"; break;
        case LIBUSB_ERROR_ACCESS: status = "AC"; break;
        case LIBUSB_ERROR_NO_DEVICE: status = "ND"; break;
        case LIBUSB_ERROR_NOT_FOUND: status = "NF"; break;
        case LIBUSB_ERROR_BUSY: status = "BS"; break;
        case LIBUSB_ERROR_TIMEOUT: status = "TO"; break;
        case LIBUSB_ERROR_OVERFLOW: status = "OF"; break;
        case LIBUSB_ERROR_PIPE: status = "PI"; break;
        case LIBUSB_ERROR_INTERRUPTED: status = "IN"; break;
        case LIBUSB_ERROR_NO_MEM: status = "NM"; break;
        case LIBUSB_ERROR_NOT_SUPPORTED: status = "NS"; break;
        case LIBUSB_ERROR_OTHER: status = "OT"; break;
        default: status = "??"; break;
    }

    printf("      {%08x%s%08x:%s}", len, dir, done, status);

    if (verbosity >= LOG_DBG){
        for (i = 0; i < 16; i++){
            if (i < done){
                printf(" %02x", ptr[i]);
            }else{
                printf("   ");
            }
        }
    }

    printf(" \"");
    for (i = 0; i < 64 && i < done; i++){
        printf("%c", isprint(ptr[i]) ? ptr[i] : '.');
    }
    printf("\"\n");
}

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
            printf("libusb_bulk_transfer failed: %s ep=0x%02x len=0x%04x size=0x%04x\n", libusb_strerror(res), ep, len, size);
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
            printf("[E] fbusb_bufcmd send incomplete: reqsz=0x%02x res=0x%04x\n", reqsz, res);
            return -1;
        }
        printf("[E] fbusb_bufcmd send failed: reqsz=0x%02x res=0x%04x\n", reqsz, res);
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

static int getvar_all(struct fbusb *dev){
    int res = fbusb_strcmd(dev, "getvar:all", (char *)rxbuff, 65);
    while (res == FASTBOOT_INFO){
        res = 0;
        if(strncmp((char *)rxbuff, "version-bootloader:", 19) == 0){
            res = 1;
        }
        if (verbosity >= LOG_NFO && (res == 1 || strncmp((char *)rxbuff, "unlocked:", 9) == 0 || strncmp((char *)rxbuff, "version-baseband:", 17) == 0 || strncmp((char *)rxbuff, "secure:", 7) == 0 || strncmp((char *)rxbuff, "product:", 8) == 0))
        {
            printf("%s\n", (char *)rxbuff);
        }
        res = fbusb_strcmd_resp(dev, (char *)rxbuff, 65);
    }
    if (res != FASTBOOT_OKAY)
    {
        if (res > 0)
        {
            printf("getvar all failed: %s\n", (char *)rxbuff);
        }
        else
        {
            printf("getvar all protocol error, res=%d\n", res);
        }
    }
    return res;
}

int main(int argc, char **argv){

    libusb_device_handle *h;

    const int vendor_id = 0x0fce, product_id = 0x0dde, inter_face = 0, endpoint_in = 0x81, endpoint_out = 0x01;

    int res;

#ifdef ANDROID_TERMUX

    libusb_context *context = NULL;
    libusb_device *device;
    int fd;
    struct libusb_device_descriptor desc;

    for(int i = 0; i < argc; i++){
        printf("%d %s:", i+1, argv[i]);
    }

    printf("\n");

    if(argc < 2){
        printf("$ termux-usb -l\n");
        printf("$ termux-usb -r /dev/bus/usb/00*/00*\n");
        printf("$ termux-usb -e %s /dev/bus/usb/00*/00*\n", argv[0]);
        return 0;
    }

    if(sscanf(argv[1], "%d", &fd) == 1){
        printf("%s", argv[1]);
    }

    libusb_set_option(NULL, LIBUSB_OPTION_WEAK_AUTHORITY);

    res = libusb_init(&context);
    if (res < 0){
        printf("[E] libusb_init failed: %s\n", libusb_strerror(res));
        goto exit;
    }

    res = libusb_wrap_sys_device(context, (intptr_t) fd, &h);
    if (res < 0){
        printf("[E] libusb_wrap_sys_device failed: %s\n", libusb_strerror(res));
        goto exit;
    }

    device = libusb_get_device(h);

    memset(&desc, 0, sizeof(struct libusb_device_descriptor));

    res = libusb_get_device_descriptor(device, &desc);
    if(res < 0){
        printf("[E] libusb_get_device_descriptor failed: %s\n", libusb_strerror(res));
        goto clean_exit;
    }
    
    printf("Vendor ID: %04x      ==     %04x\n", desc.idVendor, vendor_id);
    printf("Product ID: %04x     ==     %04x\n", desc.idProduct, product_id);
    if(desc.idVendor != vendor_id || desc.idProduct != product_id){
        goto clean_exit;
    }

#else

    res = libusb_init(NULL);
    if (res < 0){
        printf("[E] libusb_init failed: %s\n", libusb_strerror(res));
        goto exit;
    }

    // | 端末の接続を待機中... | waiting for device connection. |

    h = NULL;
    for(uint8_t i = 0; i <= 99; ++i){
        h = libusb_open_device_with_vid_pid(NULL, vendor_id, product_id);
        if(h != NULL){
            break;
        }
        printf("[E] libusb_open_device_with_vid_pid (%04x:%04x) failed (attempt %u)\n", vendor_id, product_id, i);
        sleep(1);
    }

    if (h == NULL) {
        printf("[E] Failed to find device after all attempts\n");
        goto exit;
    }

    // https://developer.mozilla.org/en-US/docs/Web/API/USBDevice/deviceClass
    // https://developer.mozilla.org/en-US/docs/Web/API/USBDevice/deviceProtocol
    // https://developer.mozilla.org/en-US/docs/Web/API/USBDevice/deviceSubclass
    if (libusb_kernel_driver_active(h, 0) == 1){
        if(libusb_detach_kernel_driver(h, 0) != 0){
            printf("[E] libusb_detach_kernel_driver failed\n");
            goto clean_exit;
        }
    }

#endif
    
    // https://developer.mozilla.org/en-US/docs/Web/API/USBDevice/claimInterface
    res = libusb_claim_interface(h, inter_face);
    if (res < 0)
    {
        printf("[E] libusb_claim_interface failed: %s\n", libusb_strerror(res));
        goto clean_exit;
    }

    struct fbusb dev;
    memset(&dev, 0, sizeof(struct fbusb));

    dev.h = h;
    dev.iface = inter_face;
    dev.epi = endpoint_in;
    dev.epo = endpoint_out;
    dev.maxsize = 16 * 1024 * 1024;
    dev.timeout = 5000;

    getvar_all(&dev);

clean_exit:
    libusb_release_interface(h, inter_face);

exit:
    libusb_close(h);
    libusb_exit(NULL);
    return 0;

}
