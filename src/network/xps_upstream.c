#include "../xps.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port)
{
    assert(core != NULL);
    assert(host != NULL);
    assert(is_valid_port(port));

    int upstream_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (upstream_fd < 0)
    {
        logger(LOG_ERROR, "xps_listener_create()", "socket() failed");
        perror("Error Messsage");
        return NULL;
    }

    struct addrinfo *addr_info = xps_getaddrinfo(host, port);
    if (addr_info == NULL)
    {
        logger(LOG_ERROR, "xps_upstream_create()", "xps_getaddrinfo() failed");
        close(upstream_fd);
        return NULL;
    }

    // struct sockaddr_in upstream_addr;
    // upstream_addr.sin_family = AF_INET;
    // upstream_addr.sin_addr.s_addr = inet_addr(host);
    // upstream_addr.sin_port = htons(port);

    int connect_error = connect(upstream_fd, addr_info->ai_addr, addr_info->ai_addrlen);

    freeaddrinfo(addr_info);

    if (connect_error < 0 && errno != EINPROGRESS)
    {
        logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
        perror("Error message");
        close(upstream_fd);
        return NULL;
    }

    xps_connection_t *connection = xps_connection_create(core, upstream_fd);
    if (!connection)
    {
        logger(LOG_ERROR, "xps_upstream_create()", "creation of connection instance failed");
        close(upstream_fd);
        return NULL;
    }

    return connection;
}