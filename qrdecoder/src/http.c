#include <stdbool.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <psp2/libssl.h>
#include <psp2/ctrl.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>

#include <psp2/io/fcntl.h>

#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "debugScreen.h"

#define printf psvDebugScreenPrintf

#define URL_DRIVER "https://drive.google.com"
#define URL_DOC "https://docs.google.com"
#define URL_DRIVER_PATTERN1 "https://drive.google.com/file/d/"
#define URL_DRIVER_DOWNLOAD "https://docs.google.com/uc?id=%s&export=download"
#define FILE_TMP "ux0:data/qrdecoder.html"
#define FILE_DEFAULT "ux0:data/index.html"
#define FILE_PATH_TEMP "ux0:data/"

enum {
    DOWNLOADED_DEST,
    DOWNLOADED_FILENAME
};

void httpInit()
{
    printf("http init\n");
    // load modules
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);

    // init net
    static char memory[16 * 1024];
    SceNetInitParam param;
    param.memory = memory;
    param.size = sizeof(memory);
    param.flags = 0;
    sceNetInit(&param);
    sceNetCtlInit();

    // init ssl & http
    sceSslInit(300 * 1024);
    int http = sceHttpInit(40 * 1024);
}

void httpTerm()
{
    printf("http term\n");
    // terminate ssl, http & net
	sceSslTerm();
    sceHttpTerm();
    sceNetCtlTerm();
    sceNetTerm();

    // Unload modules
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTPS);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
}

int debugPrintf(char *text, ...) {
    va_list list;
    char string[5120];

    va_start(list, text);
    vsprintf(string, text, list);
    va_end(list);

    SceUID fd = sceIoOpen("ux0:data/qrdecoder_log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, string, strlen(string));
        sceIoClose(fd);
    }

    return 0;
}

bool getHeaderFileName(const char *headers, int size, char *fileName)
{
    bool result = false;
    char *dispo = strstr(headers, "Content-Disposition:");
    if (dispo) {
        char *name = strstr(dispo, "filename=\"");
        if (name) {
            int len = strlen("filename=\"");
            char *quote = strchr(name + len, '"');
            strncpy(fileName, name + len, quote - (name + len));
            result = true;
        }
    }
    return result;
}

void printSize(long long int total)
{
    float size = 0 ;
    int unit = 0;
    if (total < 1024) {
        size = total;
    } else if (total < 1048576ll) {
        unit = 1;
        size = (float)total / (float)1024;
    } else if (total < 1073741824ll) {
        unit = 2;
        size = (float)total / (float)1048576ll;
    } else if (total < 1099511627776ll) {
        unit = 3;
        size = (float)total / (float)1073741824ll;
    } else {
        size = total;
    }
    if (size == 0) {
        size = 1;
    }
    switch (unit) {
    case 0:
        printf("\e[10;65Hdownloaded: %.0f Bs\n", size);
        break;
    case 1:
        printf("\e[10;65Hdownloaded: %.0f KBs\n", size);
        break;
    case 2:
        printf("\e[10;65Hdownloaded: %.2f MBs\n", size);
        break;
    case 3:
        printf("\e[10;65Hdownloaded: %.3f GBs\n", size);
        break;
    }
}

int directDownload(const char *url, const char *dest)
{
    SceCtrlData ctrl_peek;
    SceCtrlData ctrl_press;
	psvDebugScreenPrintf("\n\nDownloading %s to %s\n", url, dest);
    printf("press SQUARE to cancel download\n");

	// Create template with user agend "PS Vita Sample App"
	int tpl = sceHttpCreateTemplate("PS Vita QRDecoder/0.2  http/1.1", SCE_HTTP_VERSION_1_1, 1);
    if (tpl < 0) {
        return tpl;
    }

	// set url on the template
	int conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
	if (conn < 0) {
        return conn;
    }

	// create the request with the correct method
	int request = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
	if (request < 0) {
        return request;
    }

	// send the actual request. Second parameter would be POST data, third would be length of it.
	int handle = sceHttpSendRequest(request, NULL, 0);
	if (handle < 0) {
        return handle;
    }

    // get headers
    bool hasFileName = false;
    char filePath[1024];
    char *headers;
    unsigned int hSize;
    char fileName[512];
    memset(filePath, 0x00, sizeof(filePath));
    memset(fileName, 0x00, sizeof(fileName));
    handle = sceHttpGetAllResponseHeaders(request, &headers, &hSize);
    if (hSize > 0) {
        // debugPrintf("headers: %s\n", headers);
        if ((hasFileName = getHeaderFileName(headers, hSize, fileName))) {
            printf("filename: %s\n", fileName);
            sprintf(filePath, "%s%s", FILE_PATH_TEMP, fileName);
            printf("filePath: %s\n", filePath);
        }
    }

	// open destination file
	int fh = 0;
    if (hasFileName) {
        fh = sceIoOpen(filePath, SCE_O_WRONLY | SCE_O_CREAT, 0777);
    } else {
        fh = sceIoOpen(dest, SCE_O_WRONLY | SCE_O_CREAT, 0777);
    }
	if (fh < 0) {
        sceHttpDeleteRequest(request);
        sceHttpDeleteConnection(conn);
        sceHttpDeleteTemplate(tpl);

        return fh;
    }

	// create buffer and counter for read bytes.
	unsigned char data[16*1024];
	int read = 0;
    long long int total = 0;

	// read data until finished
    uint32_t currentY = psvGetDebugScreenCoordY();
	while ((read = sceHttpReadData(request, &data, sizeof(data))) > 0) {
        total += read;
        printSize(total);
		// writing the count of read bytes from the data buffer to the file
		int write = sceIoWrite(fh, data, read);
        ctrl_press = ctrl_peek;
        sceCtrlPeekBufferPositive(0, &ctrl_peek, 1);
        ctrl_press.buttons = ctrl_peek.buttons & ~ctrl_press.buttons;
        if (ctrl_press.buttons & SCE_CTRL_SQUARE) {
            break;
        }
	}
    psvSetDebugScreenCoordY(currentY);

	// close file
	sceIoClose(fh);

    sceHttpDeleteRequest(request);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);

    if (hasFileName) {
        printf("downloaded to %s\n", filePath);
        return DOWNLOADED_FILENAME;
    } else {
        printf("downloaded to %s\n", dest);
        return DOWNLOADED_DEST;
    }
}

bool isGoogleDriver(const char *url)
{
    return (strstr(url, URL_DOC)!=NULL) || (strstr(url, URL_DRIVER)!=NULL);
}

int strlastindex(const char *str, char ch)
{
    int len = strlen(str);
    for (int i=len-1; i>=0; i--) {
        if (*(str+i) == ch) {
            return i;
        }
    }
    return -1;
}

bool extractGoogleDriverID(const char *url, char *id)
{
    bool result = false;
    if (strstr(url, URL_DRIVER_PATTERN1) != NULL) {
        // match pattern 1
        int pos = strlastindex(url, '/');
        int baselen = strlen(URL_DRIVER_PATTERN1);
        int len = 0;
        if (pos > baselen) {
            len = pos - baselen;
        } else {
            len = strlen(url) - baselen;
        }
        if (len > 0) {
            strncpy(id, url + strlen(URL_DRIVER_PATTERN1), len);
            result = true;
        }
    } else {
        char *pid = strstr(url, "id=");
        if (pid != NULL) {
            char *pand = strstr(pid, "&");
            int len = 0;
            if (pand != NULL) {
                len = pand - pid - 3;
            } else {
                len = strlen(url) - (pid - url) - 3;
            }
            if (len > 0) {
                strncpy(id, pid + 3, len);
                result = true;
            }
        }
    }
    return result;
}

bool getDirectURL(char *url, char *id)
{
    bool result = false;
    // open destination file
	int fh = sceIoOpen(FILE_TMP, SCE_O_RDONLY, 0777);

    // stat
    SceIoStat stat;
    sceIoGetstatByFd(fh, &stat);
    if (stat.st_size > 0) {
        char *data = (char*)malloc(stat.st_size+1);
        if (data != NULL) {
            memset(data, 0x00, stat.st_size+1);
            int len = sceIoRead(fh, data, stat.st_size);
            if (len == stat.st_size) {
                char *purl = strstr(data, "/uc?export=download&amp;confirm=");
                if (purl != NULL) {
                    char *pch = strstr(purl, "confirm=");
                    char *pand = strchr(pch, '&');
                    char confirm[10];
                    memset(confirm, 0x00, sizeof(confirm));
                    strncpy(confirm, pch + strlen("confirm="), pand - (pch + strlen("confirm=")));
                    sprintf(url, "%s/uc?export=download&confirm=%s&id=%s", URL_DOC, confirm, id);
                    result = true;
                }
            }
        }
    }
    return result;
}

void googleDownload(const char *url)
{
    char tmp[1000];
    char id[100];
    memset(tmp, 0x00, sizeof(tmp));
    memset(id, 0x00, sizeof(id));
    
    httpInit();
    if (extractGoogleDriverID(url, id)) {
        sprintf(tmp, URL_DRIVER_DOWNLOAD, id);

        int downloaded = directDownload(tmp, FILE_TMP);
        if (downloaded == DOWNLOADED_DEST) {
            memset(tmp, 0x00, sizeof(tmp));
            if (getDirectURL(tmp, id)) {
                printf("direct url: %s\n", tmp);
                directDownload(tmp, FILE_DEFAULT);
            }
        }
        sceKernelDelayThread(5*1000*1000);
    }
    httpTerm();
}
