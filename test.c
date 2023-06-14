#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    struct ifaddrs *ifaddr, *ifa;

    // Get the list of network interfaces
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // Iterate over the network interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // Check if the interface is not the loopback interface
        if ((ifa->ifa_flags & IFF_LOOPBACK) == 0) {
            if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4 address
                struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(sa->sin_addr), ip, INET_ADDRSTRLEN);
                printf("%s: %s\n", ifa->ifa_name, ip);
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6) { // IPv6 address
                struct sockaddr_in6* sa = (struct sockaddr_in6*)ifa->ifa_addr;
                char ip[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &(sa->sin6_addr), ip, INET6_ADDRSTRLEN);
                printf("%s: %s\n", ifa->ifa_name, ip);
            }
        }
    }

    // Free the memory allocated by getifaddrs
    freeifaddrs(ifaddr);

    return 0;
}
