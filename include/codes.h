/*
* Grupo 12
* @author Daniel Santos 44887
* @author Luis Barros  47082
* @author Marcus Dias 44901
*/

// Codigos usados por nos neste projeto

#ifndef _CODES_H
#define _CODES_H

#define ERROR -1
#define OK 0

// Definindo os booleanos
#define true 1
#define false 0

// Definindo os tipos de comandos
#define BADKEY -1
#define PUT 1
#define GET 2
#define UPDATE 3
#define DEL 4
#define SIZE 5
#define QUIT 6

// Códigos de msgs de erros
#define SEM_ARG 70
#define PUT_NO_ARGS 71
#define NO_COMMAND 72
#define GET_NO_ARG 73
#define ERROR_SYS 74
#define UPDATE_NO_ARGS 75
#define DEL_NO_ARG 76

// Códigos de erros possiveis
// associados à tabela de servidor
#define KEY_ALREADY_EXIST 30
#define UPDATE_KEY_NOT_EXIST 31

// Códigos para definir que ip:porto 
// é o primário e o secundário
#define IP_ADDR_1 51
#define IP_ADDR_2 52

#endif