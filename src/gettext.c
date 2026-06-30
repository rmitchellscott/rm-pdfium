#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *FPDF_DOCUMENT;
typedef void *FPDF_PAGE;
typedef void *FPDF_TEXTPAGE;

static void emit_utf8(const unsigned short *u16, int n)
{
    unsigned char *out = malloc((size_t)n * 4 + 1);
    if (!out)
        return;

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
            out[outLen++] = (unsigned char)cp;
        } else if (cp < 0x800) {
            out[outLen++] = (unsigned char)(0xC0 | (cp >> 6));
            out[outLen++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[outLen++] = (unsigned char)(0xE0 | (cp >> 12));
            out[outLen++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[outLen++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else {
            out[outLen++] = (unsigned char)(0xF0 | (cp >> 18));
            out[outLen++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            out[outLen++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[outLen++] = (unsigned char)(0x80 | (cp & 0x3F));
        }
    }
    fwrite(out, 1, outLen, stdout);
    free(out);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;

    const char *path = argv[1];
    int page = atoi(argv[2]);
    if (page < 1)
        return 2;

    void *h = dlopen("/usr/lib/libpdfium.so", RTLD_NOW);
    if (!h)
        h = dlopen("libpdfium.so", RTLD_NOW);
    if (!h)
        return 3;

    void (*InitLibrary)(void) = dlsym(h, "FPDF_InitLibrary");
    FPDF_DOCUMENT (*LoadDocument)(const char *, const char *) = dlsym(h, "FPDF_LoadDocument");
    int (*GetPageCount)(FPDF_DOCUMENT) = dlsym(h, "FPDF_GetPageCount");
    FPDF_PAGE (*LoadPage)(FPDF_DOCUMENT, int) = dlsym(h, "FPDF_LoadPage");
    void (*ClosePage)(FPDF_PAGE) = dlsym(h, "FPDF_ClosePage");
    double (*GetPageHeight)(FPDF_PAGE) = dlsym(h, "FPDF_GetPageHeight");
    FPDF_TEXTPAGE (*TextLoadPage)(FPDF_PAGE) = dlsym(h, "FPDFText_LoadPage");
    void (*TextClosePage)(FPDF_TEXTPAGE) = dlsym(h, "FPDFText_ClosePage");
    int (*TextCountChars)(FPDF_TEXTPAGE) = dlsym(h, "FPDFText_CountChars");
    void (*TextGetCharBox)(FPDF_TEXTPAGE, int, double *, double *, double *, double *) = dlsym(h, "FPDFText_GetCharBox");
    int (*TextGetText)(FPDF_TEXTPAGE, int, int, unsigned short *) = dlsym(h, "FPDFText_GetText");
    void (*CloseDocument)(FPDF_DOCUMENT) = dlsym(h, "FPDF_CloseDocument");
    void (*DestroyLibrary)(void) = dlsym(h, "FPDF_DestroyLibrary");

    if (!InitLibrary || !LoadDocument || !GetPageCount || !LoadPage || !ClosePage ||
        !GetPageHeight || !TextLoadPage || !TextClosePage || !TextCountChars ||
        !TextGetCharBox || !TextGetText || !CloseDocument)
        return 3;

    InitLibrary();

    FPDF_DOCUMENT doc = LoadDocument(path, NULL);
    if (!doc)
        return 4;

    if (page > GetPageCount(doc)) {
        CloseDocument(doc);
        return 5;
    }

    int rc = 6;
    FPDF_PAGE pg = LoadPage(doc, page - 1);
    if (pg) {
        FPDF_TEXTPAGE textPage = TextLoadPage(pg);
        if (textPage) {
            int nChars = TextCountChars(textPage);
            double height = GetPageHeight(pg);

            /* reMarkable leaves the adjacent page's boundary line in each page's
               content stream, clipped off-screen; skip characters outside the
               media box. The overflow is contiguous at the stream's start/end. */
            int start = -1, end = -1;
            for (int i = 0; i < nChars; i++) {
                double cl, cr, cb, ct;
                TextGetCharBox(textPage, i, &cl, &cr, &cb, &ct);
                if (ct > height + 1.0 || cb < -1.0)
                    continue;
                if (start < 0)
                    start = i;
                end = i;
            }

            if (start >= 0) {
                int count = end - start + 1;
                unsigned short *u16 = malloc((size_t)(count + 1) * sizeof(unsigned short));
                if (u16) {
                    int got = TextGetText(textPage, start, count, u16);
                    int n = got > 0 ? got : 0;
                    if (n > 0 && u16[n - 1] == 0)
                        n--;
                    emit_utf8(u16, n);
                    free(u16);
                }
            }
            rc = 0;
            TextClosePage(textPage);
        }
        ClosePage(pg);
    }

    CloseDocument(doc);
    if (DestroyLibrary)
        DestroyLibrary();

    return rc;
}
