#include "../xps.h"

xps_config_t *xps_config_create(const char *config_path)
{
    assert(config_path != NULL);

    xps_config_t *config = malloc(sizeof(xps_config_t));
    if (config == NULL)
    {
        logger(LOG_ERROR, "xps_config_create()", "malloc failed to allocate config");

        return NULL;
    }

    JSON_Value *config_json = json_parse_file(config_path);
    if (config_json == NULL)
    {
        logger(LOG_ERROR, "xps_config_create()", "Failed to parse config file");

        free(config);
        return NULL;
    }
    config->_config_json = config_json;

    JSON_Object *root_object = json_value_get_object(config_json);
    if (root_object == NULL)
    {
        logger(LOG_ERROR, "xps_config_create()", "Root JSON value must be an object");
        goto error;
    }

    config->config_path = config_path;
    config->server_name = json_object_get_string(root_object, "server_name");
    vec_init(&(config->servers));
    vec_init(&(config->_all_listeners));

    JSON_Array *servers = json_object_get_array(root_object, "servers");
    if (servers == NULL)
    {
        logger(LOG_ERROR, "xps_config_create()", "json parser failed for servers");
        goto error;
    }

    for (size_t i = 0; i < json_array_get_count(servers); i++)
    {
        JSON_Object *server = json_array_get_object(servers, i);
        if (server == NULL)
        {
            logger(LOG_ERROR, "xps_config_create()", "json parser failed for server");
            goto error;
        }
        xps_config_server_t *server_config = malloc(sizeof(xps_config_server_t));
        if (server_config == NULL)
        {
            logger(LOG_ERROR, "xps_config_create()", "malloc failed allocating server_config");
            goto error;
        }

        vec_init(&(server_config->listeners));
        vec_init(&(server_config->hostnames));
        vec_init(&(server_config->routes));

        vec_push(&(config->servers), server_config);

        JSON_Array *listeners = json_object_get_array(server, "listeners");
        if (listeners == NULL)
        {
            logger(LOG_ERROR, "xps_config_create()", "json parser failed for listeners");
            goto error;
        }
        for (int j = 0; j < json_array_get_count(listeners); j++)
        {
            JSON_Object *listener = json_array_get_object(listeners, j);
            if (listener == NULL)
            {
                logger(LOG_ERROR, "xps_config_create()", "json parser failed for listener");
                goto error;
            }

            xps_config_listener_t *listener_config = malloc(sizeof(xps_config_listener_t));
            if (listener_config == NULL)
            {
                logger(LOG_ERROR, "xps_config_create()", "malloc failed allocating listener_config");
                goto error;
            }

            listener_config->host = json_object_get_string(listener, "host");
            listener_config->port = json_object_get_number(listener, "port");

            vec_push(&(server_config->listeners), listener_config);
            vec_push(&(config->_all_listeners), listener_config);
        }

        JSON_Array *routes = json_object_get_array(server, "routes");
        if (routes == NULL)
        {
            logger(LOG_ERROR, "xps_config_create()", "json parser failed for routes");
            goto error;
        }
        for (int j = 0; j < json_array_get_count(routes); j++)
        {
            JSON_Object *route = json_array_get_object(routes, j);
            if (route == NULL)
            {
                logger(LOG_ERROR, "xps_config_create()", "json parser failed for route");
                goto error;
            }

            xps_config_route_t *route_config = malloc(sizeof(xps_config_route_t));
            if (route_config == NULL)
            {
                logger(LOG_ERROR, "xps_config_create()", "malloc failed allocating route_config");
                goto error;
            }
            vec_init(&(route_config->index));
            vec_init(&(route_config->upstreams));

            vec_push(&(server_config->routes), route_config);

            JSON_Array *indexes = json_object_get_array(route, "index");
            if (indexes != NULL)
            {
                for (size_t k = 0; k < json_array_get_count(indexes); k++)
                {
                    const char *index = json_array_get_string(indexes, k);

                    if (index == NULL)
                    {
                        logger(LOG_ERROR, "xps_config_create()", "Invalid index entry");
                        goto error;
                    }

                    vec_push(&(route_config->index), index);
                }
            }

            JSON_Array *upstreams = json_object_get_array(route, "upstreams");
            if (upstreams != NULL)
            {
                for (size_t k = 0; k < json_array_get_count(upstreams); k++)
                {
                    const char *upstream = json_array_get_string(upstreams, k);

                    if (upstream == NULL)
                    {
                        logger(LOG_ERROR, "xps_config_create()", "Invalid upstream entry");
                        goto error;
                    }

                    vec_push(&(route_config->upstreams), upstream);
                }
            }

            route_config->req_path = json_object_get_string(route, "req_path");
            route_config->type = json_object_get_string(route, "type");
            route_config->dir_path = json_object_get_string(route, "dir_path");
            route_config->http_status_code = json_object_get_number(route, "http_status_code");
            route_config->redirect_url = json_object_get_string(route, "redirect_url");
        }

        JSON_Array *hostnames = json_object_get_array(server, "hostname");
        if (hostnames)
        {
            for (int j = 0; j < json_array_get_count(hostnames); j++)
            {
                const char *hostname = json_array_get_string(hostnames, j);
                if (hostname)
                {
                    vec_push(&(server_config->hostnames), hostname);
                }
            }
        }
    }

    return config;

error:
    xps_config_destroy(config);
    return NULL;
}

void xps_config_destroy(xps_config_t *config)
{
    for (int i = 0; i < config->servers.length; i++)
    {
        xps_config_server_t *server = config->servers.data[i];

        for (int j = 0; j < server->routes.length; i++)
        {
            xps_config_route_t *route = server->routes.data[i];

            vec_deinit(&(route->index));
            vec_deinit(&(route->upstreams));
        }

        vec_deinit(&(server->routes));
        vec_deinit(&(server->listeners));
        vec_deinit(&(server->hostnames));
    }

    vec_deinit(&(config->servers));
    vec_deinit(&(config->_all_listeners));

    json_value_free(config->_config_json);

    free(config);
}

xps_config_lookup_t *xps_config_lookup(xps_config_t *config, xps_http_req_t *http_req, xps_connection_t *client, int *error)
{
    assert(config != NULL);
    assert(http_req != NULL);
    assert(client != NULL);
    assert(error != NULL);

    *error = E_FAIL;

    // get host, accept encoding, pathname from http_req
    const char *host = xps_http_get_header(&(http_req->headers), "Host");
    const char *accept_encoding = xps_http_get_header(&(http_req->headers), "Accept-Encoding");
    const char *pathname = http_req->pathname;

    // Step 1: Find matching server block
    int target_server_index = -1;
    for (int i = 0; i < config->servers.length; i++)
    {
        xps_config_server_t *server = config->servers.data[i];

        // Check if client listener is present in server
        bool has_matching_listener = false;
        for (int j = 0; j < server->listeners.length; j++)
        {

            xps_config_listener_t *listener = server->listeners.data[j];
            if (strcmp(listener->host, client->listener->host) == 0 && listener->port == client->listener->port)
            {
                has_matching_listener = true;
                break;
            }
        }
        if (!has_matching_listener)
        {
            continue;
        }

        // Check if host header matches any hostname
        // NOTE: if not hostnames provided, it is considered a match
        bool has_matching_hostname = false;
        int j = 0;
        for (j; j < server->hostnames.length; j++)
        {
            const char *hostname = server->hostnames.data[j];

            if (strcmp(host, hostname) == 0)
            {
                has_matching_hostname = true;
                break;
            }
        }
        if (has_matching_hostname || j == server->hostnames.length)
        {
            target_server_index = i;
            break;
        }
    }

    if (target_server_index == -1)
    {
        *error = E_NOTFOUND;
        return NULL;
    }

    xps_config_server_t *server = config->servers.data[target_server_index];

    /*Find matching route block*/
    // Route matching uses prefix matching with longest-match-first strategy.
    // This is important because:
    // - For file serving routes (e.g., "/"), we need to match any path under it
    //   (e.g., "/index.html", "/css/style.css" should all match route "/")
    // - For specific routes (e.g., "/api"), we want them to take precedence over "/"
    //
    // Example: If we have routes "/" and "/api"
    // - Request "/index.html" matches "/" only → serves file from "/"
    // - Request "/api/users" matches both "/" and "/api" → use "/api" (longest match)

    xps_config_route_t *route = NULL;
    size_t best_match_len = 0; // Track the longest matching route path

    for (int j = 0; j < server->routes.length; j++)
    {
        xps_config_route_t *current_route = server->routes.data[j];
        size_t route_path_len = strlen(current_route->req_path);

        // Check if this route's path is a prefix of the request path
        if (str_starts_with(pathname, current_route->req_path))
        {
            // If this is a longer match than we've found so far, use it
            if (route_path_len > best_match_len)
            {
                route = current_route;
                best_match_len = route_path_len;
            }
        }
    }

    if (route == NULL)
    {
        *error = E_NOTFOUND; // No matching route found - 404
        return NULL;
    }

    // Init values of lookup
    xps_config_lookup_t *lookup = malloc(sizeof(xps_config_lookup_t));
    if (lookup == NULL)
    {
        *error = E_FAIL;
        return NULL;
    }
    memset(lookup, 0, sizeof(xps_config_lookup_t));

    if (!strcmp(route->type, "file_serve"))
    {
        lookup->type = REQ_FILE_SERVE;
        lookup->dir_path = route->dir_path;
    }
    else if (!strcmp(route->type, "reverse_proxy"))
    {
        lookup->type = REQ_REVERSE_PROXY;
        if (route->upstreams.length)
        {
            lookup->upstream = route->upstreams.data[0];
        }
    }
    else if (!strcmp(route->type, "redirect"))
    {
        lookup->type = REQ_REDIRECT;
        lookup->redirect_url = route->redirect_url;
        lookup->http_status_code = route->http_status_code;
    }
    vec_init(&(lookup->ip_whitelist));
    vec_init(&(lookup->ip_blacklist));

    // File serve
    if (lookup->type == REQ_FILE_SERVE)
    {
        size_t joined_path_size = strlen(lookup->dir_path) + strlen(pathname) + 4;
        char joined_path[joined_path_size];
        snprintf(joined_path, joined_path_size, "%s/%s", lookup->dir_path, pathname);

        char *resource_path = realpath(joined_path, NULL);
        if (!resource_path)
        {
            *error = HTTP_NOT_FOUND;
            xps_config_lookup_destroy(lookup);
            return NULL;
        }

        // is file
        if (is_file(resource_path))
        {
            lookup->file_path = resource_path;
        }
        else if (is_dir(resource_path))
        { // is directory
            /* If request is for a directory, serve the index file (e.g. index.html)
             * instead of showing the directory listing. */

            bool index_file_found = false;
            for (int i = 0; i < route->index.length; i++)
            {
                char *index_file = path_join(resource_path, route->index.data[i]);
                index_file_found = true;
                lookup->file_path = index_file;
                break;
            }

            free(resource_path);

            if (!index_file_found)
            {
                xps_config_lookup_destroy(lookup);
                return NULL;
            }
        }
        else
        {
            free(resource_path);
        }

        *error = OK;
        return lookup;
    }

    return NULL;
}

void xps_config_lookup_destroy(xps_config_lookup_t *config_lookup)
{
    free(config_lookup->file_path);
    vec_deinit(&(config_lookup->ip_whitelist));
    vec_deinit(&(config_lookup->ip_blacklist));

    free(config_lookup);
}