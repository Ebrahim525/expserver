#include "../xps.h"

void connection_loop_close_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    xps_connection_destroy(connection);
}
void connection_loop_write_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    xps_buffer_t *buffer = xps_buffer_list_read(connection->write_buff_list, connection->write_buff_list->len);

    ssize_t bytes_written = send(connection->sock_fd, buffer->pos, buffer->len, 0);
    if (bytes_written == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            xps_buffer_destroy(buffer);
        }
        else
        {
            xps_connection_destroy(connection);
        }
    }
    else
    {
        xps_buffer_list_clear(connection->write_buff_list, bytes_written);
        xps_buffer_destroy(buffer);
    }
}

void connection_loop_read_handler(void *ptr)
{
    /* validate params*/
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    char buff[DEFAULT_BUFFER_SIZE];
    ssize_t read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE - 1, 0);
    if (read_n < 0)
    {
        logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
        perror("Error message");
        xps_connection_destroy(connection);
        return;
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

    xps_buffer_list_t *buffer_list = xps_buffer_list_create();
    if (buffer_list == NULL)
    {
        logger(LOG_ERROR, "xps_connection_create()", "xps_buffer_list_create() failed");
        return NULL;
    }

    xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection, connection_loop_read_handler, connection_loop_write_handler, connection_loop_close_handler);

    connection->core = core;
    connection->sock_fd = sock_fd;
    connection->listener = NULL;
    connection->remote_ip = get_remote_ip(sock_fd);
    connection->write_buff_list = buffer_list;

    vec_push(&(connection->core->connections), connection);

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

    // destroy buffer list
    xps_buffer_list_destroy(connection->write_buff_list);

    /* free connection instance */
    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}