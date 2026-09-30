#include "../xps.h"

void connection_source_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;

    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, DEFAULT_BUFFER_SIZE, NULL);
    if (buff == NULL)
    {
        logger(LOG_DEBUG, "connection_source_handler()", "xps_buffer_create() failed");
        return;
    }

    int pipe_threshold = 0;
    int read_n = recv(connection->sock_fd, buff->data, pipe_threshold, 0);
    buff->len = read_n;

    // Socket would block
    if (read_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        xps_buffer_destroy(buff);
        source->ready = false;
        return;
    }

    // Socket error
    if (read_n < 0)
    {
        xps_buffer_destroy(buff);
        logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
        connection_close(connection, false);
        return;
    }

    // Peer closed connection
    if (read_n == 0)
    {
        xps_buffer_destroy(buff);
        xps_connection_destroy(source->ptr);
        return;
    }

    if (xps_pipe_source_write(source, buff) != OK)
    {
        logger(LOG_ERROR, "connection_source_handler()", "xps_pipe_source_write() failed");
        xps_buffer_destroy(buff);
        xps_connection_destroy(connection);
        return;
    }

    xps_buffer_destroy(buff);
}

void connection_source_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;

    if (!(source->active) && !(connection->sink->active))
    {
        xps_connection_destroy(connection);
    }
}

void connection_sink_handler(void *ptr)
{
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, );
    if (buff == NULL)
    {
        logger(LOG_ERROR, "connection_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    // Write to socket
    int write_n = send(/*fill this*/, MSG_NOSIGNAL);

    xps_buffer_destroy(buff);

    // Socket would block
    if (write_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        sink->ready = false;
        return;
    }

    // Socket error
    if (write_n < 0)
    {
        logger(LOG_ERROR, "connection_sink_handler()", "send() failed");
        xps_connection_destroy(connection);
        return;
    }

    if (write_n == 0)
        return;

    if (xps_buffer_list_clear(sink->pipe->buff_list, write_n) != OK)
        logger(LOG_ERROR, "connection_sink_handler()", "failed to clear %d bytes from sink", write_n);
}

void connection_sink_close_handler(void *ptr)
{
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;

    if (!(sink->active) && !(connection->source->active))
    {
        xps_connection_destroy(connection);
    }
}

void connection_close(xps_connection_t *connection, bool peer_closed)
{
    assert(connection != NULL);

    logger(LOG_INFO, "connection_close()", peer_closed ? "peer closed connection" : "closing connection");
    xps_connection_destroy(connection);
}

void connection_loop_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    xps_connection_destroy(connection);
}
void connection_write_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    xps_buffer_t *buffer = xps_buffer_list_read(connection->write_buff_list, connection->write_buff_list->len);

    ssize_t write_n = send(connection->sock_fd, buffer->pos, buffer->len, 0);

    xps_buffer_destroy(buffer);

    if (write_n == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            connection->write_ready = false;
        }
        else
        {
            xps_connection_destroy(connection);
        }

        return;
    }

    xps_buffer_list_clear(connection->write_buff_list, write_n);
}

void connection_read_handler(void *ptr)
{
    /* validate params*/
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    char buff[DEFAULT_BUFFER_SIZE];
    ssize_t read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE - 1, 0);
    if (read_n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            connection->read_ready = false;
            return;
        }
        else
        {
            logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
            perror("Error message");
            xps_connection_destroy(connection);
            return;
        }
    }
    if (read_n == 0)
    {
        logger(LOG_INFO, "xps_connection_read_handler()", "peer closed connection");
        xps_connection_destroy(connection);
        return;
    }

    buff[read_n] = '\0';

    // printf("[CLIENT MESSAGE] %s", buff);
    logger(LOG_INFO, "xps_connection_read_handler()", "[CLIENT MESSAGE] %s", buff);

    for (int start = 0, end = strlen(buff) - 2; start < end; start++, end--)
    {
        char temp = buff[start];
        buff[start] = buff[end];
        buff[end] = temp;
    }

    xps_buffer_t *buffer = xps_buffer_create(read_n, read_n, NULL);
    if (buffer == NULL)
    {
        logger(LOG_ERROR, "xps_buffer__create()", "failed");
        return;
    }
    memcpy(buffer->data, buff, read_n);

    xps_buffer_list_append(connection->write_buff_list, buffer);

    connection_loop_write_handler(ptr);
}

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd)
{
    xps_connection_t *connection = malloc(sizeof(xps_connection_t));
    if (connection == NULL)
    {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
        return NULL;
    }

    xps_pipe_source_t *source = xps_pipe_source_create(connection, xps_pipe_source_write, xps_pipe_source_destroy);
    if (source == NULL)
    {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'source'");
        free(connection);
        return NULL;
    }

    xps_pipe_sink_t *sink = xps_pipe_pink_create(connection, xps_pipe_sink_read, xps_pipe_sink_destroy);
    if (sink == NULL)
    {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'sink'");
        xps_pipe_source_destroy(source);
        free(connection);
        return NULL;
    }

    connection->core = core;
    connection->sock_fd = sock_fd;
    connection->listener = NULL;
    connection->remote_ip = get_remote_ip(sock_fd);
    connection->source = source;
    connection->sink = sink;
    vec_push(&(connection->core->connections), connection);

    if (xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection, source->handler_cb, sink->handler_cb, connection_loop_close_handler) != OK)
    {
        logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
        xps_pipe_source_destroy(source);
        xps_pipe_sink_destroy(sink);
        free(connection);
        return NULL;
    }

    logger(LOG_DEBUG, "xps_connection_create()", "created connection");
    return connection;
}

void xps_connection_destroy(xps_connection_t *connection)
{
    /* validate params */
    assert(connection != NULL);

    /* set connection to NULL in 'connections' list */
    for (int i = 0; i < connection->core->connections.length; i++)
    {
        xps_connection_t *curr = connection->core->connections.data[i];
        if (curr == connection)
        {
            connection->core->connections.data[i] = NULL;
            break;
        }
    }

    /* detach connection from loop */
    xps_loop_detach(connection->core->loop, connection->sock_fd);

    /* close connection socket FD */
    close(connection->sock_fd);

    /* free connection->remote_ip */
    free(connection->remote_ip);

    // destroy source
    xps_pipe_source_destroy(connection->source);

    // destroy sink
    xps_pipe_sink_destroy(connection->sink);

    /* free connection instance */
    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}