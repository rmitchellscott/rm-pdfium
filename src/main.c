#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "xovi.h"

typedef void *FPDF_DOCUMENT;
typedef int FPDF_BOOL;
typedef unsigned long FPDF_DWORD;

typedef struct {
    int version;
    int (*WriteBlock)(void *pThis, const void *data, unsigned long size);
} FPDF_FILEWRITE;

typedef FPDF_DOCUMENT (*fn_FPDF_LoadDocument)(const char *, const char *);
typedef FPDF_DOCUMENT (*fn_FPDF_CreateNewDocument)(void);
typedef int (*fn_FPDF_GetPageCount)(FPDF_DOCUMENT);
typedef FPDF_BOOL (*fn_FPDF_ImportPages)(FPDF_DOCUMENT, FPDF_DOCUMENT, const char *, int);
typedef FPDF_BOOL (*fn_FPDF_SaveAsCopy)(FPDF_DOCUMENT, FPDF_FILEWRITE *, FPDF_DWORD);
typedef void (*fn_FPDF_CloseDocument)(FPDF_DOCUMENT);

static fn_FPDF_LoadDocument    pfn_LoadDocument;
static fn_FPDF_CreateNewDocument pfn_CreateNewDocument;
static fn_FPDF_GetPageCount    pfn_GetPageCount;
static fn_FPDF_ImportPages     pfn_ImportPages;
static fn_FPDF_SaveAsCopy      pfn_SaveAsCopy;
static fn_FPDF_CloseDocument   pfn_CloseDocument;

static int g_resolved = 0;
static void *g_pdfium_handle = NULL;

static void *pdfium_sym(const char *name)
{
    void *sym = NULL;
    if (g_pdfium_handle)
        sym = dlsym(g_pdfium_handle, name);
    if (!sym)
        sym = dlsym(RTLD_DEFAULT, name);
    return sym;
}

static int resolve_pdfium(void)
{
    if (g_resolved)
        return 1;

    g_pdfium_handle = dlopen("libpdfium.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_pdfium_handle)
        g_pdfium_handle = dlopen("libpdfium.so", RTLD_NOW);

    pfn_LoadDocument      = (fn_FPDF_LoadDocument)pdfium_sym("FPDF_LoadDocument");
    pfn_CreateNewDocument = (fn_FPDF_CreateNewDocument)pdfium_sym("FPDF_CreateNewDocument");
    pfn_GetPageCount      = (fn_FPDF_GetPageCount)pdfium_sym("FPDF_GetPageCount");
    pfn_ImportPages       = (fn_FPDF_ImportPages)pdfium_sym("FPDF_ImportPages");
    pfn_SaveAsCopy        = (fn_FPDF_SaveAsCopy)pdfium_sym("FPDF_SaveAsCopy");
    pfn_CloseDocument     = (fn_FPDF_CloseDocument)pdfium_sym("FPDF_CloseDocument");

    if (!pfn_LoadDocument || !pfn_CreateNewDocument || !pfn_ImportPages ||
        !pfn_SaveAsCopy || !pfn_CloseDocument)
        return 0;

    g_resolved = 1;
    return 1;
}

static FILE *g_outFile = NULL;

static int write_block(void *pThis, const void *data, unsigned long size)
{
    (void)pThis;
    if (!g_outFile)
        return 0;
    return fwrite(data, 1, size, g_outFile) == size;
}

static char *error(const char *msg)
{
    char *buf = malloc(strlen(msg) + 8);
    if (buf)
        sprintf(buf, "ERROR: %s", msg);
    return buf;
}

static void child_trim(const char *srcPath, const char *dstPath, const char *pageRange)
{
    FPDF_DOCUMENT srcDoc = pfn_LoadDocument(srcPath, NULL);
    if (!srcDoc)
        _exit(1);

    FPDF_DOCUMENT dstDoc = pfn_CreateNewDocument();
    if (!dstDoc)
        _exit(1);

    if (!pfn_ImportPages(dstDoc, srcDoc, pageRange, 0))
        _exit(1);

    FILE *out = fopen(dstPath, "wb");
    if (!out)
        _exit(1);

    g_outFile = out;
    FPDF_FILEWRITE writer;
    writer.version = 1;
    writer.WriteBlock = write_block;

    FPDF_BOOL ok = pfn_SaveAsCopy(dstDoc, &writer, 0);
    fclose(out);
    g_outFile = NULL;

    _exit(ok ? 0 : 1);
}

char *trimPdf(const char *params)
{
    if (!params || !*params)
        return error("empty params");

    if (!resolve_pdfium())
        return error("PDFium not available");

    const char *p1 = strchr(params, ',');
    if (!p1)
        return error("expected: sourcePath,destPath,pageRange");
    const char *p2 = strchr(p1 + 1, ',');
    if (!p2)
        return error("expected: sourcePath,destPath,pageRange");

    char *srcPath = strndup(params, p1 - params);
    char *dstPath = strndup(p1 + 1, p2 - (p1 + 1));
    const char *pageRange = p2 + 1;

    if (!srcPath || !dstPath) {
        free(srcPath);
        free(dstPath);
        return error("allocation failed");
    }

    pid_t pid = fork();
    if (pid < 0) {
        free(srcPath);
        free(dstPath);
        return error("fork failed");
    }

    if (pid == 0) {
        child_trim(srcPath, dstPath, pageRange);
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    free(srcPath);
    free(dstPath);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return strdup("ok");

    return error("trim failed");
}

void _xovi_construct(void)
{
    resolve_pdfium();
}

char _xovi_shouldLoad(void)
{
    return 1;
}
