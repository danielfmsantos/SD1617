	/*
*		Grupo 12
* @author Daniel Santos 44887
* @author Luis Barros  47082
* @author Marcus Dias 44901
*/


/*
   Programa que implementa um servidor de uma tabela hash com chainning.
   Uso: table-serverAux <porta TCP> <dimensão da tabela>
   Exemplo de uso: ./table_serverAux 54321 10
*/
#include <error.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>


#include "../include/inet.h"
#include "../include/table-private.h"
#include "../include/message-private.h"
#include "../include/table_skel.h"
#include "../include/primary_backup-private.h"

#define ERROR -1
#define OK 0
#define CHANGE_ROUTINE 1
#define HELLO 2
#define TRUE 1 // boolean true
#define FALSE 0 // boolean false
#define NCLIENTS 10 // Número de sockets (uma para listening e uma para o stdin)
#define TIMEOUT -1 // em milisegundos
#define LOG_LENGTH 32 // Tamanho da string ip:porto para o ficheiro de log
#define RETRY_SLEEP 2
char * FILE_NAME_PRI = "priServerFile.txt"; //save info about primary server
char * FILE_NAME_SEC = "secServerFile.txt"; //save info about secundary server
char *myIP = "127.0.0.1"; //considerar que o secundario e o primario estão sempre na mesma maquina

//variaveis globais
int i;
int numFds = 2; //numero de fileDescriptors
struct pollfd socketsPoll[NCLIENTS]; // o array de fds
int isPrimary; // booleano a representar se eu sou primario
int isSecondaryOn; // booleano a representar se secundario estar ligado
int listening_socket; // listening socket
int stdin_fd; // socket do stdin (keyboard input - stdin)
int connsock; // sockets conectada
int result; // resultado de operacoes
int client_on = TRUE; // booleano cliente conectado
int serverAux_on = TRUE; // booleano servidor online
struct sockaddr_in client; // struct cliente
socklen_t size_client; // size cliente
int checkPoll; // check do poll , verificar se houve algo no poll
struct server_t *server; //servidor
char *secPort;
char *secIP;
char *myPort;
char *listSize;
int activeFDs = 0; //num de fds activos
int close_conn; 
int compress_list; //booleano representa se deve fazer compress da de socketsPoll

int dadosProntos;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dados_para_enviar = PTHREAD_COND_INITIALIZER;
struct thread_params{
	struct message_t *msg;
	int threadResult; // o resultado do envio da thread
};
struct thread_params *params;




/********************************
**
**            METODOS LOG FILES
**
**********************************/

/*
Escreve/Atualiza no ficheiro de log ip:porto do primario,
onde ip_port é o porto:ip do novo servidor primario e
e file_name é o nome do ficheiro de log
Retorna 0 em caso de sucesso, caso contrario -1
*/
int write_log(char* file_name,char* ip_port) {
	FILE *fd;	// File descriptor
	// Open file
	fd = fopen(file_name, "w+");
	// check fd
	if (fd == NULL) {return ERROR;}
	// Write in file
	fputs(ip_port, fd);
	// Fecha file descriptor
	fclose(fd);
	
	return OK;
}

/*
Le do ficheiro o ip_porto do servidor primario
Onde file_name é o nome do ficheiro de log e
ip_port_buffer é para onde vai ser copiado ip_porto do 
servidor primário atual
Retorna 0 em caso de sucesso, caso contrario, -1
*/
int read_log(char* file_name, char* ip_port_buffer) {
	FILE *fd; // File descriptor
	// Open file
	fd = fopen(file_name, "r");

	// Testa de file descriptor consegue ler o ficheiro
	if (fd == NULL) {return ERROR;}

	// Le o ficheiro para o apontador

	fread(ip_port_buffer, 1, LOG_LENGTH, fd);
	fclose(fd);
	return OK;
}

/*
Apaga o ficheiro de log
Onde file_name é o nome do ficheiro de log
Retorna 0 em caso de sucesso, coso contrário -1
*/
int destroy_log(char* file_name) {
	// Resultado da operação
	return remove(file_name);
}
/**
*	WRITE TO LOG IP AND PORT
*/
int write_to_log(char*file,char *ip, char*port){
	int result; // Devolver resultado do write_log
	destroy_log(file); //não interessa se ERROR ou OK
	char *toWrite = malloc(LOG_LENGTH);
	cluster_ip_port(toWrite, ip, port);
	// Escreve no ficheiro
	result = write_log(file, toWrite);
	
	// Libertar memoria do toWrite
	free(toWrite);
	
	// Devolve resultado
	return result;
}

/************************************************
**
**            METODOS WRITE AND READ FROM SOCKET
**
*************************************************/
/* Função que garante o envio de len bytes armazenados em buf,
   através da socket sock.
*/
int write_all(int sock, char *buf, int len){
	int bufsize = len;
	while(len > 0){
		int res = write(sock, buf, len);
		if(res == 0){
			//servidor disconnected...
			return ERROR;
		}
		if(res < 0){
			if(errno == EINTR) continue;
			perror("write failed:");
			return res;
		}
		buf+= res;
		len-= res;
	}
	return bufsize;
}
/* Função que garante a receção de len bytes através da socket sock,
   armazenando-os em buf.
*/
int read_all(int sock, char *buf, int len){
	int bufsize = len;
	while(len > 0){
		int res = read(sock, buf, len);
		if(res == 0){
			//client disconnected...
			return ERROR;
		}
		if(res < 0){
			if(errno == EINTR) continue;
			perror("read failed:");
			return res;
		}
		buf+= res;
		len-= res;
	}
	return bufsize;
}




/************************************************
**
**            METODOS AUXILIARES 
**				ENTRE PRIMARIO E SECUNDARIO
**
**
*************************************************/
void divide_ip_port(char *address_port, char *ip_ret, char *port_ret){
	// Separar os elementos da string, ip : porto	
	const char ip_port_seperator[2] = ":";
	char *p;
	// adress_por é constante
	p = strdup(address_port);
	char *token = strtok(p, ip_port_seperator);
	strcpy(ip_ret,token);
	token = strtok(NULL, ip_port_seperator);
	strcpy(port_ret, token);
	free(p);
}

void cluster_ip_port(char *ip_port_aux, char *ip_in, char *port_in){
	//juntar o ip:port
	char ip_port_seperator[2] = ":";
	//serve para nao inserir erros na string e causar erro no write to log
	ip_port_aux[0] = '\0';
	strcat(ip_port_aux, ip_in);
	strcat(ip_port_aux, ip_port_seperator);
	strcat(ip_port_aux, port_in);
}

void finishserverAux(int signal){
    //close dos sockets
    for (i = 0; i < numFds; i++){
    	if(socketsPoll[i].fd >= 0){
     		close(socketsPoll[i].fd);
     	}
 	}
	table_skel_destroy();
	printf("\n :::: -> SERVIDOR ENCERRADO <- :::: \n");
	exit(0);
}

int serverInit(char *myPort, char *listSize){
	listening_socket = make_server_socket(atoi(myPort));
	//check if done right
	if(listening_socket < 0){return -1;}
	
	/* initialize table */
	if(table_skel_init(atoi(listSize)) == ERROR){ 
		close(listening_socket); 
		return ERROR;
	}
	//inicializa todos os clientes com 0
	memset(socketsPoll, 0 , sizeof(socketsPoll));
	//o primeiro elem deve ser o listening
	socketsPoll[0].fd = listening_socket;
	socketsPoll[0].events = POLLIN;

	//o segundo elem deve ser o stdin (para capturar o "print")
	stdin_fd = STDIN_FILENO;
	socketsPoll[1].fd = stdin_fd;
	socketsPoll[1].events = POLLIN;
	return OK;
}

/* Função para preparar uma socket de receção de pedidos de ligação.
*/
int make_server_socket(short port){
  int socket_fd;
  int rc, on = 1;
  struct sockaddr_in serverAux;

  if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    perror("Erro ao criar socket");
    return -1;
  }

  //make it reusable
  rc = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
  if(rc < 0 ){
  	perror("erro no setsockopt");
  	close(socket_fd);
  	return ERROR;
  }

  serverAux.sin_family = AF_INET;
  serverAux.sin_port = htons(port);  
  serverAux.sin_addr.s_addr = htonl(INADDR_ANY);



  if (bind(socket_fd, (struct sockaddr *) &serverAux, sizeof(serverAux)) < 0){
      perror("Erro ao fazer bind");
      close(socket_fd);
      return -1;
  }

  //o segundo argumento talvez nao deva ser 0, para poder aceitar varios FD's
  if (listen(socket_fd, 0) < 0){
      perror("Erro ao executar listen");
      close(socket_fd);
      return -1;
  }
  return socket_fd;
}

/* Função "inversa" da função secundary_send_receive usada no table-client.
   Neste caso a função implementa um ciclo receive/send:

	Recebe um pedido;
	Aplica o pedido na tabela;
	Envia a resposta.
*/
int network_receive_send(int sockfd){
	char *message_resposta, *message_pedido;
	int msg_length;
	int message_size, msg_size, result;
	struct message_t *msg_pedido, *msg_resposta;
	int changeRoutine = FALSE;
	int helloP = FALSE;
	int helloSuccess = FALSE;
	int helloSpecial = FALSE;

	/* Com a função read_all, receber num inteiro o tamanho da 
	   mensagem de pedido que será recebida de seguida.*/
	result = read_all(sockfd, (char *) &msg_size, _INT);
	/* Verificar se a receção teve sucesso */
	if(result != _INT || result == ERROR){return ERROR;}

	/* Alocar memória para receber o número de bytes da
	   mensagem de pedido. */
	message_size = ntohl(msg_size);
	message_pedido = (char *) malloc(message_size);

	/* Com a função read_all, receber a mensagem de resposta. */
	result = read_all(sockfd, message_pedido, message_size);

	/* Verificar se a receção teve sucesso */
	if(result != message_size){return ERROR;}
	/* Desserializar a mensagem do pedido */
	msg_pedido = buffer_to_message(message_pedido, message_size);

	/* Verificar se a desserialização teve sucesso */
	if(msg_pedido == NULL){return ERROR;}

	//check if secundario acordou
	if(isPrimary){
		if(msg_pedido->opcode == OC_HELLO && msg_pedido->c_type == CT_KEY){
			helloP = TRUE;
			printf("hello get ip and port\n");
			struct sockaddr_in addr;
			socklen_t addr_len = sizeof(addr);
			char ipstr[LOG_LENGTH];
			int err = getpeername(sockfd, (struct sockaddr *) &addr, &addr_len);
			if (err != 0) {
				printf("erro ao obter ip do primario\n");
			}else{
				inet_ntop(AF_INET, &addr.sin_addr, ipstr, sizeof(ipstr));	
				printf("Connection established successfully with %s:%i!\n", ipstr, ntohs(addr.sin_port));
				secPort = strdup(msg_pedido->content.key);
			}
			secIP = strdup(ipstr);
			printf("divide feito %s %s \n", secIP , secPort );
			printf("secundario fez pedido hello\n");
			helloSuccess = TRUE;
		}
	}

	/* caso seja secundario, antes de meter na tabela devemos
		alterar o opcode da msg para um que a tabela perceba
		caso o opcode não seja o esperado de um servidor
		foi enviado por um cliente, logo devemos mudar de rotina*/
	if(!isPrimary){//ANTES DO INVOKE
		int opcode = msg_pedido->opcode;
		//verificar & mudar code
		if( opcode == OC_DEL_S){
			msg_pedido->opcode = OC_DEL;
		}else if( opcode == OC_UPDATE_S ){
			msg_pedido->opcode = OC_UPDATE;
		}else if( opcode == OC_PUT_S ){
			msg_pedido->opcode = OC_PUT;
		}else if( opcode == OC_HELLO_SPECIAL ){
			//primario conectou e mandou hellospecial
			helloSpecial = TRUE;
			printf("hello special no secundario\n");
			//gravar ip:porto
			 struct sockaddr_in addr;
			 socklen_t addr_len = sizeof(addr);
			 int err = getpeername(sockfd, (struct sockaddr *) &addr, &addr_len);
			 if (err != 0) {
   				printf("erro ao obter ip do primario\n");
			}else{
			 	char ipstr[LOG_LENGTH];
			 	inet_ntop(AF_INET, &addr.sin_addr, ipstr, sizeof(ipstr));	
			 	printf("Connection established successfully with %s:%i!\n", ipstr, ntohs(addr.sin_port));
			 	char *porto = msg_pedido->content.key;
			 	write_to_log(FILE_NAME_PRI, ipstr, porto);
			 }
		}

		//caso não tenha mudado, veio de um cliente
		if(opcode == msg_pedido->opcode && msg_pedido->opcode != OC_HELLO_SPECIAL){
			//mudar rotina 
			changeRoutine = TRUE;
		}
		//se nao mudou, simplesmente continua...
	}
	/* Processar a mensagem */
	if(!helloP && !helloSpecial){
		msg_resposta = invoke(msg_pedido);
		if(msg_resposta == NULL){ // erro no invoke
			return ERROR;
		}
	}else{
		msg_resposta = (struct message_t *)malloc(sizeof(struct message_t));
		if(msg_resposta == NULL){return ERROR;}
		if(helloP){
			msg_resposta->opcode = OC_HELLO;
		}else{
			msg_resposta->opcode = OC_HELLO_SPECIAL;
		}
		msg_resposta->c_type = CT_RESULT;
		if(helloP){
			if(helloSuccess){
				msg_resposta->content.result = OK;
			}else{
				msg_resposta->content.result = ERROR;
			}
		}else{
			msg_resposta->content.result = OK;
		}
	}


	/* verificar se somos primario ou secundario
	caso seja primario , verificar se opcode
	faz alteracoes na tabela,
	se sim, enviar para o secundario */
	if(isPrimary  && !helloP){ //DEPOIS DO INVOKE
		//ja temos o opcode
		int opcode = msg_pedido->opcode;
		//verificar & se for algum, mudar o opcode da mensage

		if(isSecondaryOn){
			if( opcode == OC_DEL ){
				msg_pedido->opcode = OC_DEL_S;
			}else if(opcode == OC_UPDATE ){
				msg_pedido->opcode = OC_UPDATE_S;
			}else if( opcode == OC_PUT ){
				msg_pedido->opcode = OC_PUT_S;
			}

			//caso tenha mudado, enviar para o secundario
			// E enviar somente no caso de ter sido feito com sucesso na minha table
			// e neste caso o c_type eh sempre do tipo result
			if( (opcode != msg_pedido->opcode) && (msg_resposta->content.result == OK)){
				//enviar
				printf("enviar para o secundario\n");

				params->msg = msg_pedido;
				dadosProntos = ERROR;
				pthread_cond_signal(&dados_para_enviar);
	      		pthread_mutex_unlock(&mutex);
	      		//receber resultado
	      		// void *threadRetun;pthread_join(thread, NULL);
	      		// printf(" char = %s\n", (char *) &(*threadRetun) );
	      		// int resultThread = atoi((char *)&threadRetun)
	      		while(dadosProntos != OK){}
				pthread_mutex_lock(&mutex);
				if(params->msg == NULL){
					//é pq o secundario desconectou
					printf("SECUNDARIO OFFLINE\n");

				}else if(params->threadResult == ERROR){
					//no caso de ter enviado para o cliente e tenha ocorrido erro
					//devemos tirar a entrada da nossa tabela?!
					printf("ENVIO COM ERRO\n");
				}else{
					printf("ENVIADO CORRETAMENTE\n");
				}
			}
		}
	}

	/* Serializar a mensagem recebida */
	message_size = message_to_buffer(msg_resposta, &message_resposta);
	/* Verificar se a serialização teve sucesso */
	if(message_resposta <= OK){return ERROR;}
	/* Enviar ao cliente o tamanho da mensagem que será enviada
	   logo de seguida
	*/



	msg_size = htonl(message_size);
	result = write_all(sockfd, (char *) &msg_size, _INT);
	/* Verificar se o envio teve sucesso */
	if(result != _INT){return ERROR;}

	/* Enviar a mensagem que foi previamente serializada */
	result = write_all(sockfd, message_resposta, message_size);

	/* Verificar se o envio teve sucesso */
	if(result != message_size){return ERROR;}
	/* Libertar memória */

	//free(message_resposta);
	//free(message_pedido);
	//free(msg_resposta);
	//free(msg_pedido);
	if(changeRoutine){
		return CHANGE_ROUTINE;
	}else if(helloP && helloSuccess){
		return HELLO;
	}else{
		return OK;
	}
	
}

int subRoutine(){
	//Codigo de acordo com as normas da IBM
	/*make a reusable listening socket*/
	/* ciclo para receber os clients conectados */
	if(isPrimary){
		printf("a espera de clientes - primario...\n");

		//inicializar mutex
		if (pthread_mutex_init(&mutex, NULL) != 0){
		    printf("\n mutex init failed\n");
			exit(ERROR);
		}
		//o primario ganha poder do mutex
		pthread_mutex_lock(&mutex);
		result = lancaThread();    	
	}else{
		printf("a espera de clientes - secundario...\n");
	}
	//call poll and check
	while(serverAux_on){ //while no cntrl c
		printf("server online\n");
		while((checkPoll = poll(socketsPoll, numFds, TIMEOUT)) >= 0){
			//verifica se nao houve evento em nenhum socket
			if(checkPoll == 0){
				perror("timeout expired on poll()");
				continue;
			}else {
				/* então existe pelo menos 1 poll active, QUAL???? loops ;) */
				for(i = 0; i < numFds; i++){
					//procura...0 nao houve return events
					if(socketsPoll[i].revents == 0){continue;}

					//se houve temos de ver se foi POLLIN
					if(socketsPoll[i].revents != POLLIN){
     					printf("  Error! revents = %d\n", socketsPoll[i].revents);
   
       					break;
     				}

     				//se for POLLIN pode ser no listening_socket ou noutro qualquer...
     				if(socketsPoll[i].fd == listening_socket){
     					//quer dizer que temos de aceitar todas as ligações com a nossa socket listening
						size_client = sizeof(struct sockaddr_in);
     					connsock = accept(listening_socket, (struct sockaddr *) &client, &size_client);
     					if (connsock < 0){
           					if (errno != EWOULDBLOCK){
              					perror("  accept() failed");
           			 		}
           			 		break;
          				}
          				socketsPoll[numFds].fd = connsock;
          				socketsPoll[numFds].events = POLLIN;
          				numFds++;
						printf("cliente conectado\n");
     			
     					//fim do if do listening
     				}else{
						/* não é o listening....então deve ser outro...
							etapa 4, o outro agora pode ser o stdin */
						if(socketsPoll[i].fd == stdin_fd){
							char *print = "print";
							char *buffer = NULL;
    						int read;
    						int len;
    						read = getline(&buffer, &len, stdin);
    						buffer[5] = '\0';
							// read word "print" return 0 if equals
							int equals = strcmp(print, buffer);
							if(equals == 0){
								struct message_t *msg_resposta;							
								struct message_t *msg_pedido = (struct message_t *)
										malloc(sizeof(struct message_t));
								if(msg_pedido == NULL){
									perror("Problema na criação da mensagem de pedido\n");
								}
								// codes
								msg_pedido->opcode = OC_GET;
								msg_pedido->c_type = CT_KEY;
								// Skel vai verificar se content.key == !
								msg_pedido->content.key = "!";
								msg_resposta = invoke(msg_pedido);
								if(msg_resposta == NULL){
									perror("Problema na mensagem de resposta\n");
								}								
								printf("********************************\n");
								if(isPrimary){
									printf("* servidor primario\n*\n");
								}else{
									printf("* servidor secundario\n*\n");
								}
								if(msg_resposta->content.keys[0] != NULL){ 
									int i = 0;
									struct message_t *msg_aux;
									char *key_to_print;
									msg_pedido->opcode = OC_GET;
									msg_pedido->c_type = CT_KEY;
									while(msg_resposta->content.keys[i] != NULL){

										msg_pedido->content.key = msg_resposta->content.keys[i];
										msg_aux = invoke(msg_pedido);
										if(msg_aux == NULL){
										}else{
											key_to_print = msg_aux->content.data->data;
										}
										printf("* ( chave = %s , valor = %s )\n", msg_resposta->content.keys[i], key_to_print);
										i++;
									}
								}else{
									printf("* tabela vazia\n");
								}
								printf("*\n********************************\n");						
							}	
							
						
						}else{
		 					close_conn = FALSE;
		 					client_on = TRUE;
		 					printf("cliente fez pedido\n");
		 					//while(client_on){
		 						//receive data
		 					int result = network_receive_send(socketsPoll[i].fd);
		 					if(result < 0){ 
		 						//ou mal recebida ou o cliente desconectou
		 						// -> close connection
		 						printf("cliente desconectou\n");
		 						 //fecha o fileDescriptor
		 						close(socketsPoll[i].fd);
		 						//set fd -1
		      					socketsPoll[i].fd = -1;
		      					compress_list = TRUE;
								int j;
								if (compress_list){
									compress_list = FALSE;
									for (i = 0; i < numFds; i++){
										if (socketsPoll[i].fd == -1){
				    						for(j = i; j < numFds; j++){
				        						socketsPoll[j].fd = socketsPoll[j+1].fd;
				      						}
				    						numFds--;
				    					}
									}
								}
		 					}else if(result == CHANGE_ROUTINE){
		 						/* 1) mudar rotina para o primario
								   2) deletar o log antigo (Se existir)
								   3) criar um log novo com o meu ip:porto
								   4) iniciar rotina */
		 						printf("iniciar change routine\n");
		 						isPrimary = TRUE;
		 						isSecondaryOn = FALSE;
		 						destroy_log(FILE_NAME_PRI);
		 						//write_to_log(FILE_NAME_SEC, myiP, myp);
		 						printf("fim CHANGE_ROUTINE\n");
		 						subRoutine();

		 					}else if(result == HELLO){
		 						isSecondaryOn = FALSE;
		 						result = lancaThread();
		 					    if(result == OK){
		 					    	printf("Aguarde, estamos a estabelecer ligação...\n");
		 					    	sleep(RETRY_SLEEP+RETRY_SLEEP); //esperar thread fazer connect
		 							if(isSecondaryOn){
									 	update_state(server);
								 	}else{
								 		printf("erro ao contectar com o secundario\n");
								 	}
		 						}
		 					}
						}//Fim do else de outros fd's
	   				}//fim da ligacao cliente-servidor
     			}//fim do else
			}//fim do for numFds
		}//se a lista tiver fragmentada, devemos comprimir 
	}//fim do for polls
	return OK;
}



int main(int argc, char **argv){
	// caso seja pressionado o ctrl+c
	 signal(SIGINT, finishserverAux);
	 signal(SIGPIPE, SIG_IGN); //ignore sigpipe
	
	/* o numero de argumentos eh diferente entre secundario e primario 
		primario = programa + seuPorto + ipSecundario + portoSecundario + listSize
		secundario = programa + seuPorto + listSize*/
	if(argc == 5){
		//primario deve ser 5 depois
		isPrimary = TRUE;

		myPort = argv[1];
		secIP = argv[2];
		secPort = argv[3];
		listSize = argv[4];


		//tenta conectar com algum servidor primario
		char *address_port = malloc(LOG_LENGTH);
		result = read_log(FILE_NAME_SEC,address_port);
		if(result == ERROR){
			//ficheiro nao existe -> sou primario...
			//inicializa servidor
			//write_to_log();

			struct server_t *serverAux = linkToSecServer(secIP, secPort);
			if(serverAux != NULL){
				result = sendSpecialHello(serverAux);
				if(result == OK){
					result = serverInit(myPort, listSize);
					if(result == ERROR){return ERROR;}
				}
			}
		}else{
			char * ip = malloc(16);
			char * port = malloc(6);
			divide_ip_port(address_port, ip, port);
			struct server_t *serverAux = linkToSecServer(ip,port);
			if(serverAux == NULL){
				//inicializa servidor
				//write_to_log();
				result = serverInit(myPort, listSize);
				if(result == ERROR){return ERROR;}
			}else{
				//HELLO para pedir a tabela -> sou secundario
				isPrimary = FALSE;
				if(hello(serverAux) == OK){
					result = serverInit(myPort, listSize);
					if(result == ERROR){return ERROR;}
				}else{
					return ERROR;
				}
			}
		}

        free(address_port);
		subRoutine();

	}else if(argc == 3){
		//secundario
		isPrimary = FALSE;

		myPort = argv[1];
		listSize = argv[2];

		//tenta conectar com algum servidor primario
		char *address_port = malloc(LOG_LENGTH);
		result = read_log(FILE_NAME_PRI,address_port);
		if(result == ERROR){
			//ficheiro nao existe ficheiro sobre o primario
			//logo sou secundario, mas pode haver um novo primario que era
			//secundario quando morri
			result = read_log(FILE_NAME_SEC, address_port);
			if(result == ERROR){
				result = serverInit(myPort, listSize);
				if(result == ERROR){return ERROR;}
			}else{
				char * ip = malloc(16);
				char * port = malloc(6);
				divide_ip_port(address_port, ip, port);
				struct server_t *serverAux = linkToSecServer(ip,port);
				free(ip);
				free(port);

				if(serverAux == NULL){
					//inicializa servidor
					destroy_log(FILE_NAME_SEC);
					result = serverInit(myPort, listSize);
					if(result == ERROR){return ERROR;}

				}else{
					//HELLO para pedir a tabela
					if(hello(serverAux) == OK){
						result = serverInit(myPort, listSize);
						if(result == ERROR){return ERROR;}
					}else{
						return ERROR;
					}
				}
			}
		}else{
			char * ip = malloc(16);
			char * port = malloc(6);
			divide_ip_port(address_port, ip, port);
			struct server_t *serverAux = linkToSecServer(ip,port);
			free(ip);
			free(port);

			if(serverAux == NULL){
				//inicializa servidor
				destroy_log(FILE_NAME_PRI);
				result = serverInit(myPort, listSize);
				if(result == ERROR){return ERROR;}

			}else{
				//HELLO para pedir a tabela
				if(hello(serverAux) == OK){
					result = serverInit(myPort, listSize);
					if(result == ERROR){return ERROR;}
				}else{
					return ERROR;
				}
			}
		}


        free(address_port);
		subRoutine();
	}else{
		//errou
		printf("\nUso do primario: ./serverAux <porta TCP> <IP secundario> <porta TCP secundario> <dimensão da tabela>\n");
		printf("	Exemplo de uso: ./table-serverAux 54321 127.0.0.1 54322 10\n");
		printf("Uso do secundario: ./serverAux <porta TCP> <dimensão da tabela>\n");
		printf("	Exemplo de uso: ./table-serverAux 54321 10\n\n");
		return ERROR;
	}

}//fim main





/*********************************************
**
**  METODOS UTILIZADOS PELA THREAD
**
**********************************/
int lancaThread(){
	printf("lancaThread\n");
	if(server != NULL){
		server = NULL;
	}
	if(params == NULL){
		params = (struct thread_params *) malloc(sizeof(struct thread_params));
		if(params == NULL){return ERROR;}
	}
	if(secPort == NULL || secIP == NULL){return ERROR;}
	pthread_t thread;
	long id = 1234;
	int threadCreated = pthread_create(&thread, NULL, threaded_send_receive, NULL);
	if (threadCreated){
 		printf("ERROR; return code from pthread_create() is %d\n", threadCreated);
  		exit(ERROR);
	}
	printf("fim lancaThread\n");
	return OK;
}

void *threaded_send_receive(void *parametro){
		printf("start thread\n");
		if(!isSecondaryOn){
			printf(">>>>>> connecting... <<<<<<\n");
				if(server == NULL){				
				server = linkToSecServer(secIP,secPort);
				if(server == NULL){
					sleep(RETRY_SLEEP); //esperar
					server = linkToSecServer(secIP,secPort);
					if(server == NULL){
						//destruir thread
						return NULL;
					}else{
						destroy_log(FILE_NAME_SEC);
						write_to_log(FILE_NAME_SEC, secIP, secPort);
						server->porto = secPort;
						server->ip = secIP;
						isSecondaryOn = TRUE;
					}
				}else{
					destroy_log(FILE_NAME_SEC);
					write_to_log(FILE_NAME_SEC, secIP, secPort);
					server->porto = secPort;
					server->ip = secIP;
					isSecondaryOn = TRUE;
				}
			}
		}
		printf("conectado ao secundario atraves de uma thread\n");
		dadosProntos = 0;
		//ciclo de espera e envio
		while(isSecondaryOn){
			pthread_mutex_lock(&mutex);

			while(dadosProntos == 0){
				pthread_cond_wait(&dados_para_enviar, &mutex);
			}
			//enviar para o servidor secundario

			params->threadResult = ERROR;
			struct message_t *msg_resposta;
			if(params->msg == NULL){
				//acabar thread
				params->threadResult = ERROR;
			}
			msg_resposta = secundary_send_receive(server, params->msg);
			if(msg_resposta == NULL){
				//acabar thread ocorreu um erro
				//tentar enviar novamente antes de colocar a DOWN depois do retry
				sleep(RETRY_SLEEP);
				msg_resposta = secundary_send_receive(server, params->msg);
				if(msg_resposta == NULL){
					//falhou o envio novamente
					isSecondaryOn = FALSE;
					params->msg = NULL;
					params->threadResult = ERROR;
				}else{
					//acabou bem
					if(msg_resposta->c_type == CT_RESULT){
						if(msg_resposta->content.result == OK){
							params->threadResult = OK;
						}else{
							params->threadResult = ERROR;
							isSecondaryOn = FALSE;
						}
					}else{
						params->threadResult = ERROR;
					}
				}
			}else{
				//acabou bem
				if(msg_resposta->c_type == CT_RESULT){
					if(msg_resposta->content.result == OK){
						params->threadResult = OK;
					}else{
						params->threadResult = ERROR;
					}
				}else{
					params->threadResult = ERROR;
				}

			}
			dadosProntos = 0;
			pthread_mutex_unlock(&mutex);
		}
	return NULL;
}

struct server_t*linkToSecServer(char* ip, char *port){
	struct server_t *serverAux = (struct server_t*)malloc(sizeof(struct server_t));
	
	/* Verificar parâmetro da função e alocação de memória */
	if(serverAux == NULL){ 
		printf("server aux is null\n");
		return NULL; }
	/* allocar a memoria do addrs dentro do serverAux*/
	serverAux->addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
	if(serverAux->addr == NULL){
		free(serverAux);
		printf("server addr is null\n");
		return NULL;
	}

	//fixing inet addrs
	int inet_res = inet_pton(AF_INET, ip, &(serverAux->addr->sin_addr));
	if(inet_res == -1){
		free(serverAux->addr);
		free(serverAux);
		return NULL;
	}else if(inet_res == 0){
		printf("Endereço IP não é válido\n");
		free(serverAux->addr);
		free(serverAux);
		return NULL;
	}

	// Porto	
	serverAux->addr->sin_port = htons(atoi(port));
	// Tipo
	serverAux->addr->sin_family = AF_INET;
	
	// Criação do socket
	int sockt;
	// Também pode ser usado o SOCK_DGRAM no tipo, UDP
	if((sockt = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Problema na criação do socket\n");
		free(serverAux->addr);
		free(serverAux);
		return NULL;
	}

	// Estabeleber ligação
	if(connect(sockt, (struct sockaddr *) (serverAux->addr), sizeof(struct sockaddr_in)) < 0){
		perror("Problema a conectar ao servidor\n");
		free(serverAux->addr);
		free(serverAux);
		return NULL;
	}

	/* Se a ligação não foi estabelecida, retornar NULL */
	serverAux->socket = sockt;
	return serverAux;
}

struct message_t *secundary_send_receive(struct server_t *server, struct message_t *msg){
	char *message_out;
	int message_size, msg_size, result;
	struct message_t *msg_resposta;
	/* Verificar parâmetros de entrada */
	if(server == NULL || msg == NULL){
		printf("server ou msg null\n");
		 return NULL;}

	/* Serializar a mensagem recebida */
	message_size = message_to_buffer(msg, &message_out);

	/* Verificar se a serialização teve sucesso */
	if(message_size <= 0){
		return NULL;} //ocorreu algum erro

	/* Enviar ao servidor o tamanho da mensagem que será enviada
	   logo de seguida
	*/
	msg_size = htonl(message_size);
 	result = write_all(server->socket, (char *) &msg_size, _INT); //envia o size primeiro
	/* Verificar se o envio teve sucesso */
	if(result != _INT){return NULL;}


	/* Enviar a mensagem que foi previamente serializada */
	result = write_all(server->socket, message_out, message_size);

	/* Verificar se o envio teve sucesso */
	if(result != message_size){
		return NULL;} //enviar de novo?

	/* De seguida vamos receber a resposta do servidor:*/
	/*		Com a função read_all, receber num inteiro o tamanho da 
		mensagem de resposta.*/
	result = read_all(server->socket, (char *) &msg_size, _INT);
	if(result != _INT){return NULL;}
	
	message_size = ntohl(msg_size);
	free(message_out);
	
	/*	Alocar memória para receber o número de bytes da
		mensagem de resposta.*/
	message_out = (char *) malloc(message_size);
	/*		Com a função read_all, receber a mensagem de resposta. */
	
	result = read_all(server->socket, message_out, message_size);
	if(result != message_size){
		free(message_out);
		return NULL;
	}


	/* Desserializar a mensagem de resposta */
	msg_resposta = buffer_to_message(message_out, message_size);

	/* Verificar se a desserialização teve sucesso */
	if(msg_resposta == NULL){
		free(message_out);
		return NULL;
	}
	/* Libertar memória */
	free(message_out);

	return msg_resposta;
}

/*
	Pede atualização de estado do server
	Retorna 0 em caso de sucesso e -1 em caso de insucesso
	Recolhe todas as chaves existentes na tabela servidor primario
	Por cada chave, pede o seu valor e executa 
	a operacao PUT do respetivo par {chave, valor}
	na tabela do servidor secundario
*/
int update_state(struct server_t *server) {
	struct message_t *pedido, *resposta;
	char *all ="!";

	pedido = (struct message_t*)malloc(sizeof(struct message_t));
	if(pedido == NULL){return ERROR;}

	//fazer pedido all keys
	pedido->opcode = OC_GET;
	pedido->c_type = CT_KEY;
	pedido->content.key = strdup(all);

	resposta = invoke(pedido);
	if(resposta == NULL){return ERROR;}

	if(resposta->content.keys[0] != NULL){ 
		//correr resposta
		int i = 0;
		struct message_t *msg_aux;
		pedido->opcode = OC_GET;
		pedido->c_type = CT_KEY;
		while(resposta->content.keys[i] != NULL){
			pedido->content.key = strdup(resposta->content.keys[i]);
			//pedir o value de key
			msg_aux = invoke(pedido);
			if(msg_aux == NULL){
				printf("falhou pedido key\n");
				return ERROR;
			}else{
				//montar pedido do tipo put ao secundario
				//key_to_print = msg_aux->content.data->data;
				pedido->opcode = OC_PUT_S;
				pedido->c_type = CT_ENTRY;
				pedido->content.entry = entry_create(resposta->content.keys[i], msg_aux->content.data);



				//comecar a enviar para o secundario, atraves de uma thread
				printf("enviar entry %s\n", resposta->content.keys[i]);
				params->msg = pedido;
				dadosProntos = ERROR;

				pthread_cond_signal(&dados_para_enviar);
				pthread_mutex_unlock(&mutex);

				while(dadosProntos != OK){}

				pthread_mutex_lock(&mutex);

				if(params->threadResult == ERROR){
					if(params->msg == NULL){
						printf("secund morreu\n");
						return ERROR;
					}else{
						printf("tentar enviar novamente\n");
					}
				}else{
					printf("enviou corretamente\n");
				}
			}
			pedido->opcode = OC_GET;
			pedido->c_type = CT_KEY;
			i++;
		}
	}else{
		printf("tabela vazia\n");
		return OK;
	}
	return OK;
}

/*Função usada para um servidor avisar o servidor "server" de que
 já acordou. Retorna 0 em caso de sucesso, -1 em caso de insucesso*/
int hello(struct server_t *serverr){
	printf("send hello\n");
	struct message_t *hello = (struct message_t *) malloc(sizeof(struct message_t));
	if(hello == NULL){exit(ERROR);}
	hello->opcode = OC_HELLO;
	hello->c_type = CT_KEY;
	hello->content.key = myPort;
	struct message_t *resp = secundary_send_receive(serverr, hello);
	if(resp == NULL){
		//tentar enviar hello novamente depois de um tempo
		sleep(RETRY_SLEEP);
		resp = secundary_send_receive(serverr, hello);
		if(resp == NULL){
			printf("ocorreu um erro ao conectar com o primario\n");
			return ERROR;
		}
	}else if(resp->content.result != OK){
		printf("Problema na transferencia, desligar ligação...\n");
		return ERROR;
	}
	//considerar que correu tudo bem, agr vou ficar a espera
	close(serverr->socket);
	free(serverr);
	free(hello);
	return OK;
}

int sendSpecialHello(struct server_t *serverr){
	printf("send hello\n");
	struct message_t *shello = (struct message_t *) malloc(sizeof(struct message_t));
	if(shello == NULL){exit(ERROR);}
	shello->opcode = OC_HELLO_SPECIAL;
	shello->c_type = CT_KEY;
	shello->content.key = myPort;
	struct message_t *resp = secundary_send_receive(serverr, shello);
	if(resp == NULL){
		//tentar enviar hello novamente depois de um tempo
		sleep(RETRY_SLEEP);
		resp = secundary_send_receive(serverr, shello);
		if(resp == NULL){
			printf("ocorreu um erro ao conectar com o secundario\n");
			return ERROR;
		}
	}else if(resp->content.result != OK){
		printf("Problema no hello special, desligar ligação...\n");
		return ERROR;
	}
	//considerar que correu tudo bem, agr vou ficar a espera
	close(serverr->socket);
	free(serverr);
	free(shello);
	return OK;
}
