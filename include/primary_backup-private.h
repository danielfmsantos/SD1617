#include "primary_backup.h"


struct server_t {
  int socket;
	struct sockaddr_in *addr;
 	char *ip;
 	char *porto;
 };


/*Função usada para um servidor avisar o servidor "server" de que
já acordou. Retorna 0 em caso de sucesso, -1 em caso de insucesso*/
int hello(struct server_t *server);

int sendSpecialHello(struct server_t *serverr);

/*Pede atualização de estado do server
Retorna 0 em caso de sucesso e -1 em caso de insucesso*/
int update_state(struct server_t *server);

void *threaded_send_receive(void *threadID);

struct server_t * linkToSecServer();

struct message_t *secundary_send_receive(struct server_t *server, struct message_t *msg);

/*
Escreve/Atualiza no ficheiro de log ip:porto do primario,
onde ip_port é o porto:ip do novo servidor primario e
e file_name é o nome do ficheiro de log
Retorna 0 em caso de sucesso, caso contrario -1
*/
int write_log(char* file_name, char* ip_port);

/*
Le do ficheiro o ip_porto do servidor primario
Onde file_name é o nome do ficheiro de log e
ip_port_buffer é para onde vai ser copiado ip_porto do 
servidor primário atual
Retorna 0 em caso de sucesso, caso contrario, -1
*/
int read_log(char* file_name, char* ip_port_buffer);

// Apaga o ficheiro de log
// Retorna 0 em caso de sucesso, coso contrário -1.
int destroy_log(char* file_name);


void devide_ip_port(char *address_port, char *ip_ret, char *port_ret);

void cluster_ip_port(char *address_port, char *ip_in, char *port_in);

int write_to_log();


int write_all(int sock, char *buf, int len);


int read_all(int sock, char *buf, int len);

void divide_ip_port(char *address_port, char *ip_ret, char *port_ret);

void cluster_ip_port(char *ip_port_aux, char *ip_in, char *port_in);

void finishserverAux(int signal);

int serverInit(char *myPort, char *listSize);

int make_server_socket(short port);

int network_receive_send(int sockfd);

int subRoutine();

int lancaThread();


int server_send_with_retry (struct server_t *server, struct message_t *msg_out);




