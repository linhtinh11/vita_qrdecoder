#ifndef QR_DECODER_HTTP_H
#define QR_DECODER_HTTP_H

void netInit();
void netTerm();
void httpInit();
void httpTerm();
void directDownload(const char *url, const char *dest);
bool isGoogleDriver(const char *url);
int strlastindex(const char *str, char ch);
void googleDownload(const char *url);

#endif
