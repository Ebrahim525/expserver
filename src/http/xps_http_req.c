#include "../xps.h"

int http_process_request_line(xps_http_req_t *http_req, xps_buffer_t *buff)
{
    assert(http_req != NULL);
    assert(buff != NULL);

    int error = xps_http_parse_request_line(http_req, buff);
    if (error != OK)
    {
        return error;
    }

    http_req->request_line = str_from_ptrs(http_req->request_line_start, http_req->request_line_end);
    http_req->method = str_from_ptrs(http_req->method_start, http_req->method_end);
    http_req->uri = str_from_ptrs(http_req->uri_start, http_req->uri_end);
    http_req->schema = str_from_ptrs(http_req->schema_start, http_req->schema_end);
    http_req->host = str_from_ptrs(http_req->host_start, http_req->host_end);
    http_req->path = str_from_ptrs(http_req->path_start, http_req->path_end);
    http_req->pathname = str_from_ptrs(http_req->pathname_start, http_req->pathname_end);
    http_req->http_version = str_from_ptrs(http_req->http_major, http_req->http_minor);
    http_req->port = -1;
    char *port_str = str_from_ptrs(http_req->port_start, http_req->port_end);
    if (port_str[0] != '\0')
    {
        http_req->port = atoi(port_str);
        free(port_str);
    }
    else if (http_req->schema[0] != '\0')
    {
        if (!strcmp(http_req->schema, "https"))
        {
            http_req->port = 443;
        }
        else if (!strcmp(http_req->schema, "http"))
        {
            http_req->port = 80;
        }
    }

    printf("REQUEST LINE: %s", http_req->request_line);
    printf("METHOD: %s\n", http_req->method);
    printf("URI: %s\n", http_req->uri);
    printf("SCHEMA: %s\n", http_req->schema);
    printf("HOST: %s\n", http_req->host);
    printf("PORT: %d\n", http_req->port);
    printf("PATH: %s\n", http_req->path);
    printf("PATHNAME: %s\n", http_req->pathname);
    printf("HTTP_VERSION: %s\n", http_req->http_version);

    return OK;
}

int http_process_headers(xps_http_req_t *http_req, xps_buffer_t *buff)
{
    assert(http_req != NULL);
    assert(buff != NULL);

    int error;
    while (1)
    {
        error = xps_http_parse_header_line(http_req, buff);
        if (error == E_FAIL || error == E_AGAIN)
        {
            return error;
        }
        if (error == OK || error == E_NEXT)
        {
            xps_keyval_t *header = malloc(sizeof(xps_keyval_t));
            if (header == NULL)
            {
                break;
            }

            header->key = str_from_ptrs(http_req->header_key_start, http_req->header_key_end);
            header->val = str_from_ptrs(http_req->header_val_start, http_req->header_val_end);

            vec_push(&(http_req->headers), header);

            if (error == E_NEXT)
            {
                continue;
            }
        }

        printf("\nHEADERS\n");
        for (int i = 0; i < http_req->headers.length; i++)
        {
            xps_keyval_t *header = http_req->headers.data[i];
            printf("%s: %s\n", header->key, header->val);
        }

        return OK;
    }

    logger(LOG_DEBUG, "http_process_headers()", "malloc failed for header");

    for (int i = 0; i < http_req->headers.length; i++)
    {
        xps_keyval_t *header = (xps_keyval_t *)http_req->headers.data[i];
        free(header->key);
        free(header->val);
        free(header);
    }
    vec_deinit(&http_req->headers);
    return E_FAIL;
}

xps_buffer_t *xps_http_req_serialize(xps_http_req_t *http_req)
{
    assert(http_req != NULL);

    // Serialize headers into a buffer headers_str
    xps_buffer_t *headers_str = xps_http_serialize_headers(&(http_req->headers));

    size_t final_len = strlen(http_req->request_line) + 1 + headers_str->len + 1;

    // Create instance for final buffer
    xps_buffer_t *buff = xps_buffer_create(final_len, final_len, NULL);

    // Copy everything to final buffer
    memcpy(buff->pos, http_req->request_line, strlen(http_req->request_line));
    buff->pos += strlen(http_req->request_line);
    *buff->pos = '\n';
    buff->pos += 1;

    memcpy(buff->pos, headers_str->data, headers_str->len);
    buff->pos += headers_str->len;
    *buff->pos = '\n';
    buff->pos += 1;

    buff->len = final_len;

    buff->pos = buff->data;

    xps_buffer_destroy(headers_str);

    return buff;
}

xps_http_req_t *xps_http_req_create(xps_core_t *core, xps_buffer_t *buff, int *error)
{
    assert(core != NULL);
    assert(buff != NULL);

    *error = E_FAIL;

    xps_http_req_t *http_req = malloc(sizeof(xps_http_req_t));
    if (http_req == NULL)
    {
        return NULL;
    }
    memset(http_req, 0, sizeof(xps_http_req_t));

    // Set initial parser state
    http_req->parser_state = RL_START;

    // Process request line and handle possible errors
    int error_code = http_process_request_line(http_req, buff);
    if (error_code == E_FAIL)
    {
        logger(LOG_ERROR, "xps_http_req_create()", "failed to process request line");

        free(http_req);
        *error = E_FAIL;

        return NULL;
    }
    if (error_code == E_AGAIN)
    {
        logger(LOG_DEBUG, "xps_http_req_create()", "http request parsing not complete, waiting for more data");

        *error = E_AGAIN;
        return NULL;
    }
    logger(LOG_DEBUG, "xps_http_req_create()", "processed request line successfully");

    // Process headers and handle possible errors
    error_code = http_process_headers(http_req, buff);
    if (error_code == E_FAIL)
    {
        logger(LOG_ERROR, "xps_http_req_create()", "failed to process headers");

        free(http_req);
        *error = E_FAIL;

        return NULL;
    }
    if (error_code == E_AGAIN)
    {
        logger(LOG_DEBUG, "xps_http_req_create()", "http request parsing not complete, waiting for more data");

        *error = E_AGAIN;
        return NULL;
    }
    logger(LOG_DEBUG, "xps_http_req_create()", "processed headers successfully");

    // Header length
    http_req->header_len = (size_t)(buff->len - (http_req->request_line_end - buff->data));

    // Body length is retrieved from header Content-Length
    http_req->body_len = 0;
    const char *body_len_str = xps_http_get_header(&http_req->headers, "Content-Length");
    if (body_len_str != NULL)
    {
        http_req->body_len = atoi(body_len_str);
    }

    // Host Header for HTTP/1.1
    const char *host_header = xps_http_get_header(&http_req->headers, "Host");
    if (host_header == NULL && http_req->http_version != NULL && strcmp(http_req->http_version, "1.1") == 0)
    {
        xps_http_req_destroy(core, http_req);
        *error = E_FAIL;
        return NULL;
    }

    *error = OK;
    return http_req;
}

void xps_http_req_destroy(xps_core_t *core, xps_http_req_t *http_req)
{
    assert(http_req != NULL);

    free(http_req->request_line);
    free(http_req->method);
    free(http_req->uri);
    free(http_req->schema);
    free(http_req->host);
    free(http_req->path);
    free(http_req->pathname);
    free(http_req->http_version);

    for (int i = 0; i < http_req->headers.length; i++)
    {
        xps_keyval_t *header = (xps_keyval_t *)http_req->headers.data[i];
        free(header->key);
        free(header->val);
        free(header);
    }
    vec_deinit(&http_req->headers);

    free(http_req);
}