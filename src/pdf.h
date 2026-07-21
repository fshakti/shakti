#ifndef SHAKTI_PDF_H
#define SHAKTI_PDF_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

V *bi_pdf_create(V **a, int n);
V *bi_pdf_add_page(V **a, int n);
V *bi_pdf_text_at(V **a, int n);
V *bi_pdf_save(V **a, int n);
V *bi_pdf_open(V **a, int n);
V *bi_pdf_page_count(V **a, int n);
V *bi_pdf_info(V **a, int n);
V *bi_pdf_text(V **a, int n);
V *bi_pdf_close(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif
