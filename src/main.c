#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "xovi.h"

typedef void *FPDF_DOCUMENT;
typedef void *FPDF_PAGE;
typedef void *FPDF_TEXTPAGE;
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

static int full_write(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* PDFium's page/text APIs touch process-global state that is unsafe to call
   against the host's shared, in-flight instance. We dlmopen a private copy into
   a fresh link-map namespace and initialize it independently, inside a forked
   child so a malformed PDF cannot take down the host. */
static void child_get_text(const char *srcPath, int page, int wfd)
{
    void *h = dlmopen(LM_ID_NEWLM, "/usr/lib/libpdfium.so", RTLD_NOW);
    if (!h)
        h = dlmopen(LM_ID_NEWLM, "libpdfium.so", RTLD_NOW);
    if (!h)
        _exit(3);

    void (*InitLibrary)(void) = dlsym(h, "FPDF_InitLibrary");
    fn_FPDF_LoadDocument LoadDocument = (fn_FPDF_LoadDocument)dlsym(h, "FPDF_LoadDocument");
    fn_FPDF_GetPageCount GetPageCount = (fn_FPDF_GetPageCount)dlsym(h, "FPDF_GetPageCount");
    FPDF_PAGE (*LoadPage)(FPDF_DOCUMENT, int) = dlsym(h, "FPDF_LoadPage");
    void (*PageClose)(FPDF_PAGE) = dlsym(h, "FPDF_ClosePage");
    double (*PageHeight)(FPDF_PAGE) = dlsym(h, "FPDF_GetPageHeight");
    FPDF_TEXTPAGE (*TextLoad)(FPDF_PAGE) = dlsym(h, "FPDFText_LoadPage");
    void (*TextClose)(FPDF_TEXTPAGE) = dlsym(h, "FPDFText_ClosePage");
    int (*TextCount)(FPDF_TEXTPAGE) = dlsym(h, "FPDFText_CountChars");
    void (*TextCharBox)(FPDF_TEXTPAGE, int, double *, double *, double *, double *) = dlsym(h, "FPDFText_GetCharBox");
    int (*TextGet)(FPDF_TEXTPAGE, int, int, unsigned short *) = dlsym(h, "FPDFText_GetText");
    fn_FPDF_CloseDocument DocClose = (fn_FPDF_CloseDocument)dlsym(h, "FPDF_CloseDocument");

    if (!InitLibrary || !LoadDocument || !GetPageCount || !LoadPage || !PageClose ||
        !PageHeight || !TextLoad || !TextClose || !TextCount || !TextCharBox || !TextGet || !DocClose)
        _exit(4);

    InitLibrary();
    FPDF_DOCUMENT doc = LoadDocument(srcPath, NULL);
    if (!doc)
        _exit(5);
    if (page > GetPageCount(doc)) {
        DocClose(doc);
        _exit(6);
    }

    FPDF_PAGE pg = LoadPage(doc, page - 1);
    if (pg) {
        FPDF_TEXTPAGE tp = TextLoad(pg);
        if (tp) {
            int nChars = TextCount(tp);
            double height = PageHeight(pg);

            /* reMarkable leaves the adjacent page's boundary line in each page's
               content stream, clipped off-screen; skip characters outside the
               media box. The overflow is contiguous at the stream's start/end. */
            int start = -1, end = -1;
            for (int i = 0; i < nChars; i++) {
                double l, r, b, t;
                TextCharBox(tp, i, &l, &r, &b, &t);
                if (t > height + 1.0 || b < -1.0)
                    continue;
                if (start < 0)
                    start = i;
                end = i;
            }

            if (start >= 0) {
                int count = end - start + 1;
                unsigned short *u16 = malloc((size_t)(count + 1) * sizeof(unsigned short));
                unsigned char *utf8 = malloc((size_t)count * 4 + 1);
                if (u16 && utf8) {
                    int got = TextGet(tp, start, count, u16);
                    int n = got > 0 ? got : 0;
                    if (n > 0 && u16[n - 1] == 0)
                        n--;

                    size_t outLen = 0;
                    for (int i = 0; i < n; i++) {
                        unsigned int cp = u16[i];
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
                            unsigned int lo = u16[i + 1];
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                i++;
                            }
                        }
                        if (cp < 0x80) {
                            utf8[outLen++] = (unsigned char)cp;
                        } else if (cp < 0x800) {
                            utf8[outLen++] = (unsigned char)(0xC0 | (cp >> 6));
                            utf8[outLen++] = (unsigned char)(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            utf8[outLen++] = (unsigned char)(0xE0 | (cp >> 12));
                            utf8[outLen++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                            utf8[outLen++] = (unsigned char)(0x80 | (cp & 0x3F));
                        } else {
                            utf8[outLen++] = (unsigned char)(0xF0 | (cp >> 18));
                            utf8[outLen++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                            utf8[outLen++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                            utf8[outLen++] = (unsigned char)(0x80 | (cp & 0x3F));
                        }
                    }
                    full_write(wfd, utf8, outLen);
                }
                free(utf8);
                free(u16);
            }
            TextClose(tp);
        }
        PageClose(pg);
    }
    DocClose(doc);
    _exit(0);
}

char *getPageText(const char *params)
{
    if (!params || !*params)
        return error("empty params");

    const char *comma = strchr(params, ',');
    if (!comma)
        return error("expected: sourcePath,page");

    char *srcPath = strndup(params, comma - params);
    if (!srcPath)
        return error("allocation failed");

    int page = atoi(comma + 1);
    if (page < 1) {
        free(srcPath);
        return error("page must be >= 1");
    }

    int fds[2];
    if (pipe(fds) != 0) {
        free(srcPath);
        return error("pipe failed");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        free(srcPath);
        return error("fork failed");
    }

    if (pid == 0) {
        close(fds[0]);
        child_get_text(srcPath, page, fds[1]);
        _exit(0);
    }

    close(fds[1]);
    free(srcPath);

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        close(fds[0]);
        waitpid(pid, NULL, 0);
        return error("allocation failed");
    }

    for (;;) {
        if (cap - len < 4096) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) {
                free(buf);
                close(fds[0]);
                waitpid(pid, NULL, 0);
                return error("allocation failed");
            }
            buf = nb;
            cap *= 2;
        }
        ssize_t r = read(fds[0], buf + len, cap - len - 1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0)
            break;
        len += (size_t)r;
    }
    close(fds[0]);

    int status;
    waitpid(pid, &status, 0);

    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        free(buf);
        return error("text extraction failed");
    }

    buf[len] = '\0';
    return buf;
}

void _xovi_construct(void)
{
    resolve_pdfium();
}

char _xovi_shouldLoad(void)
{
    return 1;
}
