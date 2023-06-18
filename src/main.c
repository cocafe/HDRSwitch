#include <errno.h>

#include <windows.h>
#include <winuser.h>
#include <icm.h>

#include <libjj/logging.h>
#include <libjj/opts.h>

// for MSVC
#if defined(_MSC_VER)
#pragma comment(lib, "mscms.lib")
#endif

// for MSYS2
// gcc reload_icc.c /c/Windows/System32/mscms.dll

#if defined(__MINGW32__) || defined(__MINGW64__)
extern BOOL WINAPI WcsGetCalibrationManagementState(BOOL *pbIsEnabled);
extern int WINAPI InternalRefreshCalibration(int, int);
#endif

DISPLAYCONFIG_PATH_INFO *disp_path;
DISPLAYCONFIG_MODE_INFO *disp_mode;
uint32_t path_cnt, mode_cnt;

int verbose = 0;
int list_monitor = 0;
int target_monitor = -2;
int hdr_toggle = 0;
int hdr_on = 0;
int hdr_off = 0;

lopt_noarg(hdr_on, &hdr_on, sizeof(hdr_on), &(int){ 1 }, "Turn on HDR on a monitor");
lopt_noarg(hdr_off, &hdr_off, sizeof(hdr_off), &(int){ 1 }, "Turn off HDR on a monitor");
lsopt_noarg(t, toggle, &hdr_toggle, sizeof(hdr_toggle), &(int){ 1 }, "Toggle HDR state on a monitor");
lsopt_noarg(v, verbose, &verbose, sizeof(verbose), &(int){ 1 }, "Verbose output");
lsopt_noarg(l, list, &list_monitor, sizeof(list_monitor), &(int){ 1 }, "List monitors");
lsopt_int(m, monitor, &target_monitor, sizeof(target_monitor), "Monitor index");
lsopt_noarg(a, all, &target_monitor, sizeof(target_monitor), &(int){ -1 }, "Apply to all monitors");

int color_calibration_reload(void)
{
        BOOL calibration_en = 0;

        if (WcsGetCalibrationManagementState(&calibration_en) == FALSE) {
                pr_err("WcsGetCalibrationManagementState()\n");
                return -EFAULT;
        }

        if (!calibration_en)
                return 0;

        InternalRefreshCalibration(0, 0);

        return 0;
}

int displayinfo_init(void)
{
        DISPLAYCONFIG_PATH_INFO *path;
        DISPLAYCONFIG_MODE_INFO *mode;
        uint32_t pathcnt, modecnt;
        int err = 0;

        err = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathcnt, &modecnt);
        if (err != ERROR_SUCCESS) {
                pr_err("GetDisplayConfigBufferSizes(): 0x%08x\n", err);
                return -EIO;
        }

        if (pathcnt == 0 && modecnt == 0)
                return -EINVAL;

        path = calloc(pathcnt, sizeof(DISPLAYCONFIG_PATH_INFO));
        mode = calloc(modecnt, sizeof(DISPLAYCONFIG_MODE_INFO));

        if (!path || !mode) {
                if (path)
                        free(path);

                if (mode)
                        free(mode);

                return -ENOMEM;
        }

        err = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                                 &pathcnt, path,
                                 &modecnt, mode,
                                 0);
        if (err != ERROR_SUCCESS) {
                pr_err("QueryDisplayConfig(): 0x%08x\n", err);
                err = -EFAULT;
                free(path);
                free(mode);
        }

        disp_path = path;
        disp_mode = mode;
        path_cnt = pathcnt;
        mode_cnt = modecnt;

        return err;
}

void displayinfo_deinit(void)
{
        if (disp_path)
                free(disp_path);

        if (disp_mode)
                free(disp_mode);
}

static DISPLAYCONFIG_MODE_INFO *monitor_get(uint32_t idx)
{
        if (idx >= path_cnt)
                return NULL;

        for (size_t i = 0; i < mode_cnt; i++) {
                DISPLAYCONFIG_MODE_INFO *mode = &disp_mode[i];

                if (mode->id != disp_path[idx].targetInfo.id)
                        continue;

                if (mode->infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
                        continue;

                return mode;
        }

        return NULL;
}

int color_info_get(DISPLAYCONFIG_MODE_INFO *mode, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO *info)
{
        int err;

        memset(info, 0, sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO));
        info->header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        info->header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
        info->header.adapterId.HighPart = mode->adapterId.HighPart;
        info->header.adapterId.LowPart = mode->adapterId.LowPart;
        info->header.id = mode->id;

        if (ERROR_SUCCESS != (err = DisplayConfigGetDeviceInfo(&info->header))) {
                pr_err("DisplayConfigGetDeviceInfo(): 0x%08x\n", err);
                return -EIO;
        }

        return 0;
}

int color_info_set(DISPLAYCONFIG_MODE_INFO *mode, DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE *state)
{
        int err;

        state->header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        state->header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
        state->header.adapterId.HighPart = mode->adapterId.HighPart;
        state->header.adapterId.LowPart = mode->adapterId.LowPart;
        state->header.id = mode->id;

        if (ERROR_SUCCESS != (err = DisplayConfigSetDeviceInfo(&state->header))) {
                pr_err("DisplayConfigSetDeviceInfo(): 0x%08x\n", err);
                return -EIO;
        }

        return 0;
}

int hdr_get(uint32_t idx)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info;
        DISPLAYCONFIG_MODE_INFO *mode;
        int ret;

        mode = monitor_get(idx);
        if (!mode)
                return -ENOENT;

        if ((ret = color_info_get(mode, &info)))
                return ret;

        if (info.advancedColorSupported && info.advancedColorEnabled)
                return 1;

        return 0;
}

int hdr_support_get(uint32_t idx)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info;
        DISPLAYCONFIG_MODE_INFO *mode;
        int ret;

        mode = monitor_get(idx);
        if (!mode)
                return -ENOENT;

        if ((ret = color_info_get(mode, &info)))
                return ret;

        if (info.advancedColorSupported)
                return 1;

        return 0;
}

int hdr_set(uint32_t idx, uint32_t enabled)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info = { 0 };
        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE state = { 0 };
        DISPLAYCONFIG_MODE_INFO *mode;
        int err;
        
        mode = monitor_get(idx);
        if (!mode)
                return -ENOENT;

        if ((err = color_info_get(mode, &info)))
                return err;

        if (!info.advancedColorSupported)
                return -ENOTSUP;

        if (info.advancedColorEnabled == !!enabled)
                return 0;

        state.enableAdvancedColor = !!enabled;
        if ((err = color_info_set(mode, &state)))
                return err;

        Sleep(2 * 1000);
        color_calibration_reload();
        
        return 0;
}

int colordepth_get(uint32_t idx)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info;
        DISPLAYCONFIG_MODE_INFO *mode;
        int ret;

        mode = monitor_get(idx);
        if (!mode)
                return -ENOENT;

        if ((ret = color_info_get(mode, &info)))
                return ret;

        return info.bitsPerColorChannel;
}

int sdr_level_get(uint32_t idx)
{
        DISPLAYCONFIG_SDR_WHITE_LEVEL info = { 0 };
        DISPLAYCONFIG_MODE_INFO *mode;
        int err;

        mode = monitor_get(idx);
        if (!mode)
                return -ENOENT;

        info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        info.header.size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL);
        info.header.adapterId.HighPart = mode->adapterId.HighPart;
        info.header.adapterId.LowPart = mode->adapterId.LowPart;
        info.header.id = mode->id;

        if (ERROR_SUCCESS != (err = DisplayConfigGetDeviceInfo(&info.header))) {
                pr_err("DisplayConfigGetDeviceInfo(): 0x%08x\n", err);
                return -EIO;
        }

        return (int)info.SDRWhiteLevel;
}

void monitor_info_list(void)
{
        pr_raw("Monitor Resolution  HDRSupported HDREnabled ColorDepth SDRWhiteLevel\n");

        for (size_t i = 0; i < path_cnt; i++) {
                DISPLAYCONFIG_MODE_INFO *mode = monitor_get(i);
                DISPLAYCONFIG_2DREGION *size = &mode->targetMode.targetVideoSignalInfo.activeSize;
                int sdr_level = sdr_level_get(i);

                pr_raw("%-7ju %-5ux%5u %12d %10d %10d %4d (%-3.0fnits)\n",
                       i, size->cx, size->cy, hdr_support_get(i), hdr_get(i), colordepth_get(i), sdr_level, (float)sdr_level / 1000.0 * 80.0);
        }
}

int wmain(int wargc, wchar_t *wargv[])
{
        int err;

        setbuf(stdout, NULL);

        logging_colored_set(0);
        console_alloc_set(0);
        opts_helptxt_defval_print(0);

        if ((err = wchar_longopts_parse(wargc, wargv, NULL)))
                return err;

        if ((err = logging_init()))
                return err;

        console_hide();

        displayinfo_init();

        if (target_monitor >= (int)path_cnt) {
                pr_err("Invalid monitor index\n");
                goto end;
        }

        if (!verbose) {
                g_logprint_level &= ~LOG_LEVEL_VERBOSE;
        }

        if (list_monitor) {
                monitor_info_list();
                goto end;
        }

        if (hdr_toggle) {
                if (target_monitor < 0) {
                        pr_rawlvl(ERROR, "Toggle HDR needs to specify a monitor\n");
                        goto end;
                }

                if ((err = hdr_get(target_monitor)) < 0) {
                        goto end;
                }

                err = hdr_set(target_monitor, !err);

                goto end;
        }

        if (target_monitor == -1) {
                if (hdr_on) {
                        for (size_t i = 0; i < path_cnt; i++) {
                                if ((err = hdr_set(i, 1)))
                                        goto end;
                        }
                } else if (hdr_off) {
                        for (size_t i = 0; i < path_cnt; i++) {
                                if ((err = hdr_set(i, 0)))
                                        goto end;
                        }
                }

                goto end;
        }

        if (hdr_on || hdr_off) {
                if (target_monitor < 0) {
                        pr_rawlvl(ERROR, "Toggle HDR needs to specify a monitor\n");
                        goto end;
                }

                if (hdr_on) {
                        err = hdr_set(target_monitor, 1);
                } else if (hdr_off) {
                        err = hdr_set(target_monitor, 0);
                }

                goto end;
        }

end:
        displayinfo_deinit();

        logging_exit();

        return err;
}
