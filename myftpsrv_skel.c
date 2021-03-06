#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>

#include <arpa/inet.h>
#include <errno.h>


#define BUFSIZE 512
#define CMDSIZE 5
#define PARSIZE 100

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"

/**
 * function: receive the commands from the client
 * sd: socket descriptor
 * operation: \0 if you want to know the operation received
 *            OP if you want to check an especific operation
 *            ex: recv_cmd(sd, "USER", param)
 * param: parameters for the operation involve
 * return: only usefull if you want to check an operation
 *         ex: for login you need the seq USER PASS
 *             you can check if you receive first USER
 *             and then check if you receive PASS
 **/
bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // receive the command in the buffer and check for errors
    recv_s=recv(sd,buffer,BUFSIZE,0);
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // expunge the terminator characters from the buffer
    buffer[strcspn(buffer, "\r\n")] = 0;

    // complex parsing of the buffer
    // extract command receive in operation if not set \0
    // extract parameters of the operation in param if it needed
    token = strtok(buffer, " ");
    if (token == NULL || strlen(token) < 4) {
        warn("not valid ftp command");
        return false;
    } else {
        if (operation[0] == '\0') {strcpy(operation, token);
#ifdef DEBUG
            printf("Token: %s. Operation: %s\n",token,operation);
#endif
        }
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        if (token != NULL) strcpy(param, token);
    }
#ifdef DEBUG
            printf("Token: %s. Operation: %s. param: %s\n",token,operation,param);
#endif
    return true;
}

/**
 * function: send answer to the client
 * sd: file descriptor
 * message: formatting string in printf format
 * ...: variable arguments for economics of formats
 * return: true if not problem arise or else
 * notes: the MSG_x have preformated for these use
 **/
bool send_ans(int sd, char *message, ...){
    char buffer[BUFSIZE];

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);
    // send answer preformated and check errors
    return (send(sd,buffer,sizeof(buffer),0)>0);
}

/**
 * function: RETR operation
 * sd: socket descriptor
 * file_path: name of the RETR file
 **/

void retr(int sd, char *file_path) {
    FILE *file;    
    int bread;
    long fsize;
    char buffer[BUFSIZE];


    // check if file exists if not inform error to client
    if((file=fopen(file_path,"r"))==NULL){
        printf("Open file error: ");
        send_ans(sd,MSG_550,file_path);
        return;
    }
    // send a success message with the file length
    fseek(file, 0L, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0L, SEEK_SET);

    send_ans(sd,MSG_299,file_path,fsize);

    // important delay for avoid problems with buffer size
    sleep(1);

    // send the file
    bread=BUFSIZE;
    while(bread==BUFSIZE){
        if((bread=fread(buffer,1,sizeof(buffer),file))<0){
            printf("Read file error\n");
            return;
        }
        if(send(sd,buffer,bread,0)<0){
            printf("Send file error\n");
            return;
        }
    }
    // close the file
    fclose(file);
    // send a completed transfer message
    sleep(1);
    send_ans(sd,MSG_226);
#ifdef DEBUG
    printf("Se completó el envío\n");
#endif

}
/**
 * funcion: check valid credentials in ftpusers file
 * user: login user name
 * pass: user password
 * return: true if found or false if not
 **/
bool check_credentials(char *user, char *pass) {
    FILE *file;
    char *path = "./ftpusers", *line = NULL, cred[100];
    size_t len = 0;
    bool found = false;

    // make the credential string
    strcpy(cred,user);
    strcat(cred,":");
    strcat(cred,pass);
    strcat(cred,"\n");
#ifdef DEBUG
    printf("Clave buscada: %s\n",cred);
#endif
    // check if ftpusers file it's present
    if((file=fopen(path,"r"))==NULL){
        perror("Open file error: ");
        exit(errno);
    }
    // search for credential string
    while(getline(&line,&len,file)>0){
        if(!strcmp(line,cred)){
#ifdef DEBUG
            printf("Usuario: %s y contraseña: *******, encontrados.\n",user);
#endif
            found=true;
            break;
        }
    }

    // close file and release any pointes if necessary
    fclose (file);
    // return search status
    return found;

}

/**
 * function: login process management
 * sd: socket descriptor
 * return: true if login is succesfully, false if not
 **/
bool authenticate(int sd) {
    char user[PARSIZE], pass[PARSIZE];

    // wait to receive USER action
    if(!recv_cmd(sd,"USER",user)){
        printf("Incorrect command user %s",user);
        return false;
    }

    // ask for password
    send_ans(sd,MSG_331,user);
    // wait to receive PASS action
    if(!recv_cmd(sd,"PASS",pass)){
        printf("Incorrect user or password\n");
        return false;
    }

    // if credentials don't check denied login
    if(!check_credentials(user,pass)){
        send_ans(sd,MSG_530);
        printf("Incorrect user or password\n");
        close(sd);
        return false;
    }
    // confirm login
    send_ans(sd,MSG_230,user);
    return true;
}

/**
 *  function: execute all commands (RETR|QUIT)
 *  sd: socket descriptor
 **/

void operate(int sd) {
    char op[CMDSIZE], param[PARSIZE];

    while (true) {
        op[0] = param[0] = '\0';
        // check for commands send by the client if not inform and exit
        recv_cmd(sd,op,param);
        if (strcmp(op, "RETR") == 0) {
            retr(sd, param);
        } else if (strcmp(op, "QUIT") == 0) {
            // send goodbye and close connection
            send_ans(sd,MSG_221);
            break;
        } else {
            // invalid command
            printf("Invalid command: %s\n",op);
            break;
            // furute use
        }
    }
    close(sd);
    return;
}

/**
 * Run with
 *         ./mysrv <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    // arguments checking
    if(argc!=2){
        printf("Try %s <port>\n",argv[0]);
        return -1;
    }
    for(int i = 0; i < strlen(argv[1]);i++){
        if(argv[1][i] <'0'|| argv[1][i]>'9'){
            printf("\nPort must be numeric\n");
            exit(-1);
        }
    }


#ifdef DEBUG
    printf("Argumentos ok!\n");
#endif

    // reserve sockets and variables space
    int sd,sd2,sin_size;
    struct sockaddr_in addr,cliente;
    
    // create server socket and check errors
    if((sd = socket(AF_INET,SOCK_STREAM,0))==-1){
        close(sd);
        perror("Socket error: ");
        exit(errno);
    }
#ifdef DEBUG
    printf("Socket creado\n");
#endif
    
    // bind master socket and check errors
    addr.sin_family=AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr=INADDR_ANY;

    if(bind(sd,(struct sockaddr *) &addr,sizeof(addr))<0){
        close(sd);
        perror("Bind error: ");
        exit(errno);
    }
#ifdef DEBUG
    printf("Bind ok!\n");
#endif
    
    // make it listen
    if(listen(sd,BUFSIZE)==-1){
        close(sd);
        perror("Listen error: ");
        exit(errno);
    }

    // main loop
#ifdef DEBUG
    printf("Listen ok!\n");
#endif
  
    sin_size=sizeof(struct sockaddr_in);
    while (true) {
        // accept connectiones sequentially and check errors
#ifdef DEBUG
        printf("Esperando conexión...\n");
#endif
        if((sd2 = accept(sd,(struct sockaddr *)&cliente,(socklen_t *)&sin_size))==-1){
            close(sd2);
            continue;
        }
#ifdef DEBUG
        printf("Conexión entrante desde: %s\n",inet_ntoa(cliente.sin_addr));
#endif

        // send hello
        send_ans(sd2,MSG_220);

        // operate only if authenticate is true
        if(authenticate(sd2)){
#ifdef DEBUG
            printf("Autenticacion ok!\n");
#endif
            operate(sd2);
#ifdef DEBUG
            printf("Fin operacion\n");
#endif
            sleep(1);
        }

    }

    // close server socket
    close(sd);
#ifdef DEBUG
    printf("Fin\n");
#endif
    return 0;
}
