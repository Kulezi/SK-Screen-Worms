#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "err.h"
#include "gui.h"

#define QUEUE_LENGTH 5

int init_net (unsigned short port) {
  int sock;  // gniazdko początkowe
  struct sockaddr_in6 server_address;
  struct sockaddr_in6 client_address;
  socklen_t client_address_len;
  int optval;

  sock = socket(PF_INET6, SOCK_STREAM, 0);  // IPv6 TCP
  if (sock < 0)
    syserr("socket");

  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    syserr("setsockopt");

  // Ustalanie namiaru na siebie
  server_address.sin6_family = AF_INET6;
  server_address.sin6_addr = in6addr_any;
  server_address.sin6_port = htons(port);
  if (bind(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    syserr("Addres zajęty");

  // Pasywne otwarcie
  if (listen(sock, QUEUE_LENGTH) < 0)
    syserr("listen");

  // Dla przypomnienia
  printf("Zapraszamy na port %d\n", port);

  // Czekamy na propozycje
  client_address_len = sizeof(client_address);
  gsock = accept(sock, (struct sockaddr *)&client_address, &client_address_len);
  if (gsock < 0)
    syserr("accept");

  if (setsockopt(gsock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    syserr("setsockopt");

  if (fcntl(gsock, F_SETFL, O_NONBLOCK) < 0)  // tryb nieblokujący
    syserr("fcntl");

  return gsock;
}

/*EOF*/
