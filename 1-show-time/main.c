/**
 * The following piece of code guarantees the compatibility between Windows and 
 * Unix-based platforms. The #if defined(_WIN32) statement includes header files
 * that will be compiled only in windows platforms. On the other case the files
 * needed in Unix-based platforms are compiled.
*/
#if defined(_WIN32)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define GETSOCKETERRNO() (WSAGetLastError())
    #define CLOSESOCKET(socket) closesocket(socket)
    #define ISVALIDSOCKET(socket) ((socket) != INVALID_SOCKET)
#else // Header files included in Unix-based platforms
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #define GETSOCKETERRNO() (errno)
    #define CLOSESOCKET(socket) (close(socket))
    #define ISVALIDSOCKET(socket) ((socket) >= 0)
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>

#define RECV_BUFFER_SIZE 1024

int main()
{
    // Initialising Winsock
    #if defined(_WIN32)
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d))
        {
            fprintf(stderr, "Failed to initialize.\n");
            return 1;
        }
    #endif

    printf("Configuring local address...\n");
    // addrinfo struct holds info about the host when used by the getaddrinfo() function
    // About addrinfo struct: https://learn.microsoft.com/en-us/windows/win32/api/ws2def/ns-ws2def-addrinfoa
    ADDRINFOA hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Indicates the use of IPV4
    hints.ai_socktype = SOCK_STREAM; // Check SOCK_STREAM on the winsock api docs
    hints.ai_protocol = IPPROTO_TCP; // Indicates the use of TCP, if not specified than bind_address->ai_protocol is goint to be 0
    hints.ai_flags = AI_PASSIVE;

    ADDRINFOA *bind_address = NULL;
    
    /**
     * About getaddrinfo function: https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo.
     * getaddrinfo(0, "http", &hints, &bind_address); 
     *  -The second parameter is provided by %WINDIR%\system32\drivers\etc\services
     *  -Error 10045 occurs when hints.ai_socktype = SOCK_DGRAM and hints.ai_protocol = IPPROTO_UDP,
     *  probably because http uses TCP connection.
     *  -Error 10043 occur when hints.ai_socktype = SOCK_DGRAM or hints.ai_protocol = IPPROTO_UDP, because
     *  the listen() function is not supported by the UDP protocol.
    */ 
    if (getaddrinfo(0, "3000", &hints, &bind_address))
    {
        fprintf(stderr, "getaddrinfo() failed. (%d)\n", GETSOCKETERRNO());
        #if defined(_WIN32)
            WSACleanup();
        #endif
        return 1;
    }

    printf("Creating socket...\n");
    SOCKET socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);

    if (!ISVALIDSOCKET(socket_listen))
    {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        #if defined(_WIN32)
            WSACleanup();
        #endif
        return 1;
    }

    printf("Binding socket to call address...\n");
    // About bind function: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-bind
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen))
    {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        #if defined(_WIN32)
            WSACleanup();
        #endif
        return 1;
    }
    freeaddrinfo(bind_address);

    printf("Listening...\n");
    // About listen function: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-listen
    if (listen(socket_listen, 10) < 0)
    {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        #if defined(_WIN32)
            WSACleanup();
        #endif
        return 1;
    }

    printf("Waiting for connection...\n");
    struct sockaddr_storage client_address;
    socklen_t client_len = sizeof(client_address);
    // About accept function: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-accept
    SOCKET socket_client = accept(socket_listen, (struct sockaddr *) &client_address, &client_len);
    /**
     * Apparently closing this socket at this point causes no error to the program. But if it is closed before
     * the accept function it causes the 10038 error.
    */
    CLOSESOCKET(socket_listen);

    if (!ISVALIDSOCKET(socket_client))
    {
        fprintf(stderr, "accept() failed. (%d)\n", GETSOCKETERRNO());
        #if defined(_WIN32)
            WSACleanup();
        #endif
        return 1;
    }

    printf("Client is connected...\n");
    char address_buffer[100];
    // About getnameinfo function: https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getnameinfo
    getnameinfo((struct sockaddr *) &client_address, client_len, address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
    printf("%s\n", address_buffer);

    printf("Reading request...\n");
    char request[RECV_BUFFER_SIZE];
    // About recv function: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recv
    int bytes_received = recv(socket_client, request, RECV_BUFFER_SIZE, 0);
    printf("Received %d bytes.\n", bytes_received);
    printf("Sending response...\n");
    // About send function: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-send
    //int bytes_sent = send(socket_client, response, strlen(response), 0);
    //printf("Sent, %d of %d bytes.\n", bytes_sent, (int) strlen(response), 0);

    time_t timer;
    time(&timer);
    // The response is written like that on purpose for tests. The book shows a better of sending the response
    char response[1024] = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\nLocal time is: ";
    char *time = ctime(&timer);
    strcat(response, time);

    int bytes_sent = send(socket_client, response, strlen(response), 0);
    printf("Sent %d of %d bytes.\n", bytes_sent, (int) strlen(response));
    printf("Closing connection...\n");
    CLOSESOCKET(socket_client);

    #if defined(_WIN32)
        WSACleanup();
    #endif

    printf("Finished.\n");

    return 0;
}