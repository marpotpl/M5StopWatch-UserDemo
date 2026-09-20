# ESP-IDF 5.5.4 transport with a local fix in transport_ws.c. Keep all other
# sources and public headers from the installed IDF component.
set(idf_tcp_transport "${IDF_PATH}/components/tcp_transport")
set(srcs
    "${idf_tcp_transport}/transport.c"
    "${idf_tcp_transport}/transport_ssl.c"
    "${idf_tcp_transport}/transport_internal.c")

if(CONFIG_LWIP_IPV4)
    list(APPEND srcs "${idf_tcp_transport}/transport_socks_proxy.c")
endif()

if(CONFIG_WS_TRANSPORT)
    list(APPEND srcs "transport_ws.c")
endif()

set(req esp-tls)
if(NOT ${IDF_TARGET} STREQUAL "linux")
    list(APPEND req lwip esp_timer)
endif()

idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "${idf_tcp_transport}/include"
    PRIV_INCLUDE_DIRS "${idf_tcp_transport}/private_include"
    REQUIRES ${req})
