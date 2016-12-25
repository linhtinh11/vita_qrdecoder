#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/camera.h>
#include "quirc.h"
#include "debugScreen.h"
#include "http.h"
#include <psp2/apputil.h>
#include <psp2/shellutil.h>

#define ARRAYSIZE(x) (sizeof(x)/sizeof(*x))
#define DISPLAY_WIDTH 640
#define DISPLAY_HEIGHT 368
#define printf psvDebugScreenPrintf
#define DEFAULT_FILENAME "index.html"
#define DEFAULT_FOLDERPATH "ux0:temp/"
#define URL_TEST "https://drive.google.com/open?id=0B9LOWaw-o26PTmJmOXl2TU5Hc1E"

enum {
	COMMANDS_SCAN,
	COMMANDS_QUIT,
	COMMANDS_OPEN_BROWSER,
    COMMANDS_DOWNLOAD
};

static int lock_power = 0;

void powerLock()
{
	if (!lock_power)
		sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);

	lock_power++;
}

void powerUnlock()
{
	if (lock_power)
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);

	lock_power--;
	if (lock_power < 0)
		lock_power = 0;
}

int menu(char *payload)
{
	psvDebugScreenInit();
	psvDebugScreenClear(psvDebugScreenColorBg);
	SceCtrlData ctrl_peek;
	SceCtrlData ctrl_press;
	int command = -1;

	printf("select menu:\n");
	printf("+ press X to scan qrcode (hold X again to exit camera)\n");
	printf("+ press START to exit\n");
	if (payload != NULL) {
        printf("\n");
		printf("current payload: %s\n", payload);
		printf("+ press O to open browser with payload\n");
        printf("+ press TRIANGLE to download\n");
	}

	do {
		ctrl_press = ctrl_peek;
		sceCtrlPeekBufferPositive(0, &ctrl_peek, 1);
		ctrl_press.buttons = ctrl_peek.buttons & ~ctrl_press.buttons;

		if (ctrl_press.buttons & SCE_CTRL_START) {
			command = COMMANDS_QUIT;
		} else if (ctrl_press.buttons & SCE_CTRL_CROSS) {
			command = COMMANDS_SCAN;
		} else if (payload != NULL && ctrl_press.buttons & SCE_CTRL_CIRCLE) {
			command = COMMANDS_OPEN_BROWSER;
		} else if (payload != NULL && ctrl_press.buttons & SCE_CTRL_TRIANGLE) {
			command = COMMANDS_DOWNLOAD;
		}
	} while (command == -1);

	return command;
}

bool scan(char **payload)
{
	printf("start scanning\n");
	sceKernelDelayThread(1000*1000);
	bool found = false;
	void* base;
	SceUID memblock = sceKernelAllocMemBlock("camera", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * 1024 * 5, NULL);
	sceKernelGetMemBlockBase(memblock, &base);
	SceDisplayFrameBuf dbuf = { sizeof(SceDisplayFrameBuf), base, DISPLAY_WIDTH, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT};

	struct quirc *qr;
    qr = quirc_new();
    if (!qr) {
	    sceKernelFreeMemBlock(memblock);
		return found;
    }
    if (quirc_resize(qr, 640, 480) < 0) {
	    sceKernelFreeMemBlock(memblock);
		return found;
    }

	int cur_res = 1;// 1-8 (maybe more)
	int cur_cam = 1;//front:0, back:1
	int cur_fmt = 5;

	SceCtrlData ctrl_peek;
	SceCtrlData ctrl_press;
	size_t res[2] = {640,480};
	size_t fps = 30;
	SceSize size = 4*res[0]*res[1];
	sceCameraOpen(cur_cam, &(SceCameraInfo){
		.size = sizeof(SceCameraInfo),
		.format = cur_fmt,//422_TO_ABGR
		.resolution = cur_res,
		.framerate = fps,
		.sizeIBase = size,
		.pitch     = 4*(DISPLAY_WIDTH-res[0]),
		.pIBase = base,
	});
	sceCameraStart(cur_cam);
	do {
		ctrl_press = ctrl_peek;
		sceCtrlPeekBufferPositive(0, &ctrl_peek, 1);
		ctrl_press.buttons = ctrl_peek.buttons & ~ctrl_press.buttons;
		
		if (sceCameraIsActive(cur_cam)>0) {
			SceCameraRead read = {sizeof(SceCameraRead),0/*<Blocking*/};
			sceCameraRead(cur_cam, &read);
			char tmp, *pTmp;
			uint8_t *image;
		    int w, h;

		    image = quirc_begin(qr, &w, &h);
			for (int i = 0; i < size; i+=4) {
				pTmp = (char*)base;
				tmp = 0.114*pTmp[i+1] + 0.587*pTmp[i+2] + 0.299*pTmp[i+3];
				image[i/4] = tmp;
			}
			quirc_end(qr);
			int num_codes;
			int i;
			num_codes = quirc_count(qr);
		    for (i = 0; i < num_codes; i++) {
			    struct quirc_code code;
			    struct quirc_data data;
			    quirc_decode_error_t err;
			    quirc_extract(qr, i, &code);
			    /* Decoding stage */
			    err = quirc_decode(&code, &data);
			    if (!err && !found) {
			    	int len = strlen(data.payload) + 1;
			    	*payload = malloc(len);
			    	memset(*payload, 0x00, len);
			    	memcpy(*payload, data.payload, len - 1);
			    	found = true;
			    }
		    }
			if (!found) {
				sceDisplaySetFrameBuf(&dbuf, SCE_DISPLAY_SETBUF_NEXTFRAME);
		    }
		}
	} while(!found && !(ctrl_press.buttons & SCE_CTRL_CROSS));
	sceCameraStop(cur_cam);
	sceCameraClose(cur_cam);
	quirc_destroy(qr);
	sceKernelFreeMemBlock(memblock);
	return found;
}

void open(char *payload)
{
	printf("start opening browser\n");
	sceKernelDelayThread(1000*1000);
	// init app util
	int result;
	SceAppUtilInitParam initParam;
	memset(&initParam, 0x00, sizeof(SceAppUtilInitParam));
	SceAppUtilBootParam bootParam;
	memset(&bootParam, 0x00, sizeof(SceAppUtilBootParam));
	bootParam.appVersion = 1;
	result = sceAppUtilInit(&initParam, &bootParam);
	printf("init util: %d\n", result);

	SceAppUtilWebBrowserParam param;
	param.launchMode = 0;
	param.reserved = 0;
	param.str = payload;
	param.strlen = strlen(param.str);
	result = sceAppUtilLaunchWebBrowser(&param);
	printf("launch browser: %d\n", result);

	sceAppUtilShutdown();
	printf("shutdown util: %d\n", result);
}

void getFileName(char *url, char *name)
{
    int index = strlastindex(url, '/');
    int len;
    if (index < 0 || index == strlen(url)-1) {
        len = strlen(DEFAULT_FOLDERPATH) + strlen(DEFAULT_FILENAME) + 1;
        memset(name, 0x00, len);
        sprintf(name, "%s%s", DEFAULT_FOLDERPATH, DEFAULT_FILENAME);
    } else {
        len = strlen(DEFAULT_FOLDERPATH) + strlen(url+index+1) + 1;
        memset(name, 0x00, len);
        sprintf(name, "%s%s", DEFAULT_FOLDERPATH, url+index+1);
    }
}

void download(char *payload)
{
	httpInit();

    char path[2048];
    getFileName(payload, path);
	directDownload(payload, path);

	httpTerm();
	sceKernelDelayThread(2*1000*1000);
}

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	psvDebugScreenInit();
	sceShellUtilInitEvents(0);
    char *payload = NULL;
    int command;
    bool found;

	do {
		command = menu(payload);
		switch (command) {
		case COMMANDS_SCAN:
			if (payload) {
				free(payload);
				payload = NULL;
			}
			found = scan(&payload);
			break;
		case COMMANDS_OPEN_BROWSER:
			open(payload);
			break;
		case COMMANDS_DOWNLOAD:
			powerLock();
			if (!isGoogleDriver(payload)) {
				download(payload);
			} else {
			    googleDownload(payload);
			}
			powerUnlock();
			break;
		}
	} while (!(command == COMMANDS_QUIT));
	
	if (payload) {
		free(payload);
	}
	sceKernelExitProcess(0);
	return 0;
}
