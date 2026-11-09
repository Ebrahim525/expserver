#include "../xps.h"

void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff)
{
    assert(session != NULL);

    session->to_client_buff = buff;

    if (buff == NULL)
    {
        session->client_source->ready = false;
        session->upstream_sink->ready = true;
        session->file_sink->ready = true;
    }
    else
    {
        session->client_source->ready = true;
        session->upstream_sink->ready = false;
        session->file_sink->ready = false;
    }
}

void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff)
{
    assert(session != NULL);

    session->from_client_buff = buff;

    if (buff == NULL)
    {
        session->client_sink->ready = true;
        session->upstream_source->ready = false;
    }
    else
    {
        session->client_sink->ready = false;
        session->upstream_source->ready = true;
    }
}

void session_check_destroy(xps_session_t *session)
{
    assert(session != NULL);

    bool c2u_flow = session->upstream_source->active && (session->client_sink->active || session->from_client_buff);

    bool u2c_flow = session->client_source->active && (session->upstream_sink->active || session->to_client_buff);

    bool f2c_flow = session->client_source->active && (session->file_sink->active || session->to_client_buff);

    bool flowing = c2u_flow || u2c_flow || f2c_flow;

    if (!flowing)
    {
        xps_session_destroy(session);
    }
}

void upstream_error_res(xps_session_t *session)
{
    assert(session != NULL);

    session->upstream_error_res_set = true;
}

void session_process_request(xps_session_t *session)
{
    assert(session != NULL);

    xps_http_res_t *http_res = NULL;

    if (!(session->http_req) || session->http_req->path[0] == '\0')
    {
        http_res = xps_http_res_create(session->core, HTTP_BAD_REQUEST);
        if (http_res == NULL)
        {
            logger(LOG_ERROR, "session_process_request()", "xps_http_res_create() failed");
            return;
        }

        xps_buffer_t *buff = xps_http_res_serialize(http_res);
        set_to_client_buff(session, buff);

        xps_http_res_destroy(http_res);
    }

    char file_path[DEFAULT_BUFFER_SIZE];
    strcpy(file_path, "public");
    strcat(file_path, session->http_req->path);

    int error;
    session->file = xps_file_create(session->core, file_path, &error);

    if (error == E_PERMISSION)
    {
        http_res = xps_http_res_create(session->core, HTTP_FORBIDDEN);
        if (http_res == NULL)
        {
            logger(LOG_ERROR, "session_process_request()", "xps_http_res_create() failed");
            return;
        }

        xps_buffer_t *buff = xps_http_res_serialize(http_res);
        set_to_client_buff(session, buff);

        xps_http_res_destroy(http_res);
    }
    else if (error == E_NOTFOUND)
    {
        http_res = xps_http_res_create(session->core, HTTP_NOT_FOUND);
        if (http_res == NULL)
        {
            logger(LOG_ERROR, "session_process_request()", "xps_http_res_create() failed");
            return;
        }

        xps_buffer_t *buff = xps_http_res_serialize(http_res);
        set_to_client_buff(session, buff);

        xps_http_res_destroy(http_res);
    }
    else if (error == E_FAIL)
    {
        http_res = xps_http_res_create(session->core, HTTP_INTERNAL_SERVER_ERROR);
        if (http_res == NULL)
        {
            logger(LOG_ERROR, "session_process_request()", "xps_http_res_create() failed");
            return;
        }

        xps_buffer_t *buff = xps_http_res_serialize(http_res);
        set_to_client_buff(session, buff);

        xps_http_res_destroy(http_res);
    }
    else
    {
        http_res = xps_http_res_create(session->core, HTTP_OK);
        if (http_res == NULL)
        {
            logger(LOG_ERROR, "session_process_request()", "xps_http_res_create() failed");
            return;
        }

        if (session->file->mime_type)
        {
            if (xps_http_set_header(&(http_res->headers), "Content-Type", session->file->mime_type) != OK)
            {
                logger(LOG_ERROR, "session_process_request()", "Failed to add Content-Type to header");
                return;
            }
        }

        char file_size[32];
        sprintf(file_size, "%zu", session->file->size);
        if (xps_http_set_header(&(http_res->headers), "Content-Length", file_size) != OK)
        {
            logger(LOG_ERROR, "session_process_request()", "Failed to add Content-Length to header");
            return;
        }

        xps_buffer_t *buff = xps_http_res_serialize(http_res);
        set_to_client_buff(session, buff);

        xps_http_res_destroy(http_res);

        if (!xps_pipe_create(session->core, DEFAULT_BUFFER_SIZE, session->file->source, session->file_sink))
        {
            logger(LOG_ERROR, "session_process_request()", "Failed to create file pipe");

            xps_session_destroy(session);
            return;
        }
    }
}

void client_source_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;

    // write to session->to_client_buff
    if (xps_pipe_source_write(source, session->to_client_buff) != OK)
    {
        logger(LOG_ERROR, "client_source_handler()", "xps_pipe_source_write() failed");
        return;
    }
    xps_buffer_destroy(session->to_client_buff);

    set_to_client_buff(session, NULL);
    session_check_destroy(session);
}

void client_source_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;

    session_check_destroy(session);
}

void client_sink_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
    if (buff == NULL)
    {
        logger(LOG_ERROR, "client_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    if (session->http_req == NULL)
    {
        int error;
        xps_http_req_t *http_req = xps_http_req_create(session->core, buff, &error);

        xps_buffer_destroy(buff);

        if (error == E_FAIL)
        {
            logger(LOG_ERROR, "client_sink_handler()", "failed to parse http request, error code: %d", error);

            xps_http_req_destroy(session->core, http_req);
            session_process_request(session);
            return;
        }

        if (error == E_AGAIN)
        {
            logger(LOG_DEBUG, "client_sink_handler()", "http request parsing not complete, waiting for more data");

            xps_http_req_destroy(session->core, http_req);
            return;
        }

        session->http_req = http_req;

        logger(LOG_DEBUG, "client_sink_handler()", "parsed http request successfully");

        xps_buffer_t *http_req_buff = xps_http_req_serialize(http_req);

        set_from_client_buff(session, http_req_buff);
        size_t to_clear = sink->pipe->buff_list->len;
        xps_pipe_sink_clear(sink, to_clear);

        session_process_request(session);
    }
    else
    {
        set_from_client_buff(session, buff);
        xps_pipe_sink_clear(sink, buff->len);
    }
}

void client_sink_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    session_check_destroy(session);
}

void upstream_source_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;

    if (xps_pipe_source_write(source, session->from_client_buff) != OK)
    {
        logger(LOG_ERROR, "upstream_source_handler()", "xps_pipe_source_write() failed");
        return;
    }

    // Checking if upstream is connected
    if (session->upstream_connected == false)
    {
        session->upstream_write_bytes += session->from_client_buff->len;

        if (session->upstream_write_bytes > session->upstream_source->pipe->buff_list->len)
        {
            session->upstream_connected = true; // true
        }
    }

    xps_buffer_destroy(session->from_client_buff);

    set_from_client_buff(session, NULL);
    session_check_destroy(session);
}

void upstream_source_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;

    if (!(session->upstream_connected) && !(session->upstream_error_res_set))
    {
        upstream_error_res(session);
    }

    session_check_destroy(session);
}

void upstream_sink_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    session->upstream_connected = true;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
    if (buff == NULL)
    {
        logger(LOG_ERROR, "upstream_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    set_to_client_buff(session, buff);
    xps_pipe_sink_clear(sink, buff->len);
}

void upstream_sink_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    if (!session->upstream_connected && !session->upstream_error_res_set)
    {
        upstream_error_res(session);
    }

    session_check_destroy(session);
}

void file_sink_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
    if (buff == NULL)
    {
        logger(LOG_ERROR, "file_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    set_to_client_buff(session, buff);
    xps_pipe_sink_clear(sink, buff->len);
}

void file_sink_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    session_check_destroy(session);
}

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client)
{
    assert(core != NULL);
    assert(client != NULL);

    // Alloc memory for session instance
    xps_session_t *session = malloc(sizeof(xps_session_t));
    if (session == NULL)
    {
        logger(LOG_ERROR, "xps_session_create()", "malloc() failed for 'session'");
        return NULL;
    }

    session->client_source = xps_pipe_source_create(session, client_source_handler, client_source_close_handler);
    session->client_sink = xps_pipe_sink_create(session, client_sink_handler, client_sink_close_handler);
    session->upstream_source = xps_pipe_source_create(session, upstream_source_handler, upstream_source_close_handler);
    session->upstream_sink = xps_pipe_sink_create(session, upstream_sink_handler, upstream_sink_close_handler);
    session->file_sink = xps_pipe_sink_create(session, file_sink_handler, file_sink_close_handler);

    if (!(session->client_source && session->client_sink && session->upstream_source &&
          session->upstream_sink && session->file_sink))
    {
        logger(LOG_ERROR, "xps_session_create()", "failed to create some sources/sinks");

        if (session->client_source)
        {
            xps_pipe_source_destroy(session->client_source);
        }
        if (session->client_sink)
        {
            xps_pipe_sink_destroy(session->client_sink);
        }
        if (session->upstream_source)
        {
            xps_pipe_source_destroy(session->upstream_source);
        }
        if (session->upstream_sink)
        {
            xps_pipe_sink_destroy(session->upstream_sink);
        }
        if (session->file_sink)
        {
            xps_pipe_sink_destroy(session->file_sink);
        }

        free(session);
        return NULL;
    }

    // Init values
    session->core = core;
    session->client = client;
    session->upstream = NULL;
    session->upstream_connected = false;
    session->upstream_error_res_set = false;
    session->upstream_write_bytes = 0;
    session->file = NULL;
    session->http_req = NULL;
    set_to_client_buff(session, NULL);
    set_from_client_buff(session, NULL);

    // Add current session to core->sessions
    vec_push(&(core->sessions), session);

    // Attach client
    if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, client->source, session->client_sink) == NULL || xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->client_source, client->sink) == NULL)
    {
        logger(LOG_ERROR, "xps_session_create()", "failed to create client pipes");

        if (session->client_source)
        {
            xps_pipe_source_destroy(session->client_source);
        }
        if (session->client_sink)
        {
            xps_pipe_sink_destroy(session->client_sink);
        }
        if (session->upstream_source)
        {
            xps_pipe_source_destroy(session->upstream_source);
        }
        if (session->upstream_sink)
        {
            xps_pipe_sink_destroy(session->upstream_sink);
        }
        if (session->file_sink)
        {
            xps_pipe_sink_destroy(session->file_sink);
        }

        free(session);
        return NULL;
    }

    logger(LOG_DEBUG, "xps_session_create()", "created session");

    // if (client->listener->port == 8001)
    // {
    //     xps_connection_t *upstream = xps_upstream_create(core, "127.0.0.1", 3000);
    //     if (upstream == NULL)
    //     {
    //         logger(LOG_ERROR, "xps_session_create()", "xps_upstream_create() failed");
    //         perror("Error message");
    //         xps_session_destroy(session);
    //         return NULL;
    //     }
    //     session->upstream = upstream;
    //     xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, upstream->source, session->upstream_sink);
    //     xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->upstream_source, upstream->sink);
    // }

    // else if (client->listener->port == 8002)
    // {
    //     int error;
    //     xps_file_t *file = xps_file_create(core, "public/sample.txt", &error);
    //     if (file == NULL)
    //     {
    //         logger(LOG_ERROR, "xps_session_create()", "xps_file_create() failed");
    //         perror("Error message");
    //         xps_session_destroy(session);
    //         return NULL;
    //     }
    //     session->file = file;

    //     xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, file->source, session->file_sink);
    // }

    return session;
}

void xps_session_destroy(xps_session_t *session)
{
    assert(session != NULL);

    logger(LOG_DEBUG, "xps_session_destroy()", "destroying session");

    // destroy client_source, client_sink, upstream_source, upstream_sink and file_sink attached to session
    if (session->client_source)
    {
        xps_pipe_source_destroy(session->client_source);
    }
    if (session->client_sink)
    {
        xps_pipe_sink_destroy(session->client_sink);
    }
    if (session->upstream_source)
    {
        xps_pipe_source_destroy(session->upstream_source);
    }
    if (session->upstream_sink)
    {
        xps_pipe_sink_destroy(session->upstream_sink);
    }
    if (session->file_sink)
    {
        xps_pipe_sink_destroy(session->file_sink);
    }

    if (session->to_client_buff != NULL)
    {
        xps_buffer_destroy(session->to_client_buff);
    }
    if (session->from_client_buff != NULL)
    {
        xps_buffer_destroy(session->from_client_buff);
    }
    if (session->http_req != NULL)
    {
        xps_http_req_destroy(session->core, session->http_req);
    }

    // Set NULL in core's list of sessions
    for (int i = 0; i < session->core->sessions.length; i++)
    {
        if (session->core->sessions.data[i] == session)
        {
            session->core->sessions.data[i] = NULL;
            session->core->n_null_sessions++;
            break;
        }
    }

    free(session);

    logger(LOG_DEBUG, "xps_session_destroy()", "destroyed session");
}