#include "fastfetch.h"

#include <dirent.h>

static bool parseProcessName(FFstrbuf* name)
{
    if(ffStrbufIgnCaseCompS(name, "kwin_wayland") == 0 || ffStrbufIgnCaseCompS(name, "kwin_x11") == 0)
        ffStrbufSetS(name, "KWin");
    else
        return false;

    return true;        
}

void ffPrintWM(FFinstance* instance)
{
    DIR* proc = opendir("/proc/");
    if(proc == NULL)
    {
        ffPrintError(instance, "WM", "opendir(\"/proc/\") == NULL");
        return;
    }

    struct dirent* dirent;

    FFstrbuf name;
    ffStrbufInit(&name);

    bool found = false;

    while((dirent = readdir(proc)) != NULL)
    {
        if(dirent->d_type != DT_DIR)
            continue;

        char path[20];
        sprintf(path, "/proc/%.8s/comm", dirent->d_name);

        char comm[17]; //MAX_COMM_LENGTH is 17 + null terminator
        ffGetFileContent(path, comm, sizeof(comm));

        ffStrbufSetS(&name, comm);

        if((found = parseProcessName(&name)))
            break;
    }

    closedir(proc);

    if(!found)
    {
        ffPrintError(instance, "WM", "No process name matches the name of known display managers");
        return;
    }

    if(ffStrbufIsEmpty(&instance->config.wmFormat))
    {
        ffPrintLogoAndKey(instance, "WM");
        ffStrbufWriteTo(&name, stdout);
    }
    else
    {
        FFstrbuf wm;
        ffStrbufInit(&wm);

        ffParseFormatString(&wm, &instance->config.wmFormat, 1,
            (FFformatarg){FF_FORMAT_ARG_TYPE_STRBUF, &name}
        );

        ffPrintLogoAndKey(instance, "WM");
        ffStrbufWriteTo(&wm, stdout);
        
        ffStrbufDestroy(&wm);
    }

    putchar('\n');
}