#include "../xps.h"

xps_http_res_t *xps_http_res_create(xps_core_t *core, u_int code)
{
    xps_http_res_t *http_res = malloc(sizeof(xps_http_res_t));
    if (http_res == NULL)
    {
        logger(LOG_ERROR, "xps_http_res_create()", "failed to alloc memory for http_res. malloc() returned NULL");
        return NULL;
    }

    // Response Line
    sprintf(http_res->response_line, "HTTP/1.1 %u %s\r\n", code, xps_http_status_text(code));

    // Headers
    vec_init(&(http_res->headers));

    time_t now = time(NULL);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    xps_http_set_header(&(http_res->headers), "Date", time_buf);
    xps_http_set_header(&(http_res->headers), "Server", SERVER_NAME);
    xps_http_set_header(&(http_res->headers), "Access-Control-Allow-Origin", "*");

    // Body
    http_res->body = NULL;

    return http_res;
}

void xps_http_res_destroy(xps_http_res_t *res)
{
    assert(res != NULL);

    for (int i = 0; i < res->headers.length; i++)
    {
        xps_keyval_t *header = res->headers.data[i];
        free(header->key);
        free(header->val);
        free(header);
    }
    vec_deinit(&(res->headers));
    free(res->body);

    free(res);
}

xps_buffer_t *xps_http_res_serialize(xps_http_res_t *http_res)
{
    assert(http_res != NULL);

    // Serialize headers
    xps_buffer_t *headers_str = xps_http_serialize_headers(&(http_res->headers));
    if (headers_str == NULL)
    {
        logger(LOG_ERROR, "xps_http_res_serialize()", "failed to serialize headers");
        return NULL;
    }

    // Calculate length for final buffer
    size_t final_len = strlen(http_res->response_line) + headers_str->len + 2 + (http_res->body ? http_res->body->len : 0);

    // Create instance for final buffer
    xps_buffer_t *buff = xps_buffer_create(final_len, final_len, NULL);
    if (buff == NULL)
    {
        logger(LOG_ERROR, "xps_http_res_serialize()", "failed to create buffer instance");
        xps_buffer_destroy(headers_str);
        return NULL;
    }

    // copy response line
    size_t response_line_len = strlen(http_res->response_line);
    memcpy(buff->pos, http_res->response_line, response_line_len);
    buff->pos += response_line_len;

    // copy headers
    memcpy(buff->pos, headers_str->data, headers_str->len);
    buff->pos += headers_str->len;

    *buff->pos = '\r';
    buff->pos += 1;
    *buff->pos = '\n';
    buff->pos += 1;

    // copy response body
    if (http_res->body)
    {
        memcpy(buff->pos, http_res->body->data, http_res->body->len);
        buff->pos += http_res->body->len;
    }

    xps_buffer_destroy(headers_str);

    return buff;
}