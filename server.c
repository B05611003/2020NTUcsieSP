#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

typedef struct {
	char hostname[512];  // server's hostname
	unsigned short port;  // port to listen
	int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
	int id; //customer id
	int adultMask;
	int childrenMask;
} Order;
typedef struct {
	char host[512];  // client's host
	int conn_fd;  // fd to talk with client
	char buf[512];  // data sent by/to client
	size_t buf_len;  // bytes used by buf
	Order order;
	struct flock lock;
	// you don't need to change this.
	int id;
	int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";
const char* init = "Please enter the id (to check how many masks can be ordered):\n";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance


int handle_read(request* reqP) {
	char buf[512];
	read(reqP->conn_fd, buf, sizeof(buf));
	memcpy(reqP->buf, buf, strlen(buf));
	return 0;
}

int main(int argc, char** argv) {

	// Parse args.
	if (argc != 2) {
		fprintf(stderr, "usage: %s [port]\n", argv[0]);
		exit(1);
	}

	struct sockaddr_in cliaddr;  // used by accept()
	int clilen;

	int new_conn_fd;  // fd for a new connection with client
	int file_fd;  // fd for file that we open for reading
	char buf[515];
	int buf_len;

	// Initialize server
	init_server((unsigned short) atoi(argv[1]));

	// Loop for handling connections
	fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

	fd_set read_fds,master;
	FD_ZERO(&master);
	FD_SET(svr.listen_fd, &master);

	file_fd = open("preorderRecord", O_RDWR);
	struct flock mutex;
	int sel;

	while (1) {
		// Add IO multiplexing
		FD_ZERO(&read_fds);
		read_fds=master;
		// Check new connection
		if(sel = select(maxfd+1,&read_fds,NULL,NULL,NULL) > 0){
			for(int i=0;i<maxfd;i++){
				if(FD_ISSET(requestP[i].conn_fd, &read_fds) && i != svr.listen_fd){
					printf("i=%d\n", i);
	// TODO: handle requests from clients
#ifdef READ_SERVER      
					//read_in Order
					int nowid;
					Order now;
					// read from client
					int ret = handle_read(&requestP[i]);
					sscanf(requestP[i].buf, "%d", &nowid);
					//if not vaild ID
					if (nowid < 902001 || nowid > 902020){
						sprintf(buf,"Operation failed.\n");	
					}
					//if id vaild
					else{
						//set lock status
						requestP[i].lock.l_type = F_RDLCK;
						requestP[i].lock.l_whence = SEEK_SET; 
						requestP[i].lock.l_start = (nowid-902001)*sizeof(Order);
						requestP[i].lock.l_len = sizeof(Order);
						//check lock
						int check = fcntl(file_fd, F_SETLK, &requestP[i].lock);
						//if locked
						if( check==-1 ){
							sprintf(buf,"Locked.\n");
						}
						//if not lock -> read from file
						else{
							pread(file_fd, &now, sizeof(Order), (nowid-902001)*sizeof(Order));
							sprintf(buf,"You can order %d adult mask(s) and %d children mask(s).\n",now.adultMask, now.childrenMask);
							//onlock file
							requestP[i].lock.l_type=F_UNLCK;
							fcntl(file_fd, F_SETLK, &requestP[i].lock);
						}	
					}
					write(requestP[i].conn_fd, buf, strlen(buf));
					close(requestP[i].conn_fd);
					free_request(&requestP[i]);
					FD_CLR(requestP[i].conn_fd,&master);
// for write server					
#else 
					int nowid;
					// Order now;
					////new requests
					if (requestP[i].wait_for_write == 0){
						//read from client
						int ret = handle_read(&requestP[i]);
						sscanf(requestP[i].buf, "%d", &nowid);
						// if ID not vaild 
						if (nowid < 902001 || nowid > 902020){
							sprintf(buf,"Operation failed.\n");	
						}
						// if ID vaild
						else{
							//set lock status
							requestP[i].lock.l_type = F_WRLCK;
							requestP[i].lock.l_whence = SEEK_SET; 
							requestP[i].lock.l_start = (nowid-902001)*sizeof(Order);
							requestP[i].lock.l_len = sizeof(Order);
							//check lock
							int check = fcntl(file_fd, F_SETLK, &requestP[i].lock);
							// if locked
							if(check==-1){
								sprintf(buf,"Locked.\n");
								write(requestP[i].conn_fd, buf, strlen(buf));
								close(requestP[i].conn_fd);
								free_request(&requestP[i]);
								FD_CLR(requestP[i].conn_fd,&master);
							}
							else{
								//check if there exist same ID which is inuse for read or write
								int inuse=0;
								for(int j=0;j<maxfd;j++)
									if(j != i && requestP[j].id == nowid)	inuse = 1;
								// if some client inuse
								if(inuse){
									sprintf(buf,"Locked\n");
									write(requestP[i].conn_fd, buf, strlen(buf));
									close(requestP[i].conn_fd);
									free_request(&requestP[i]);
									FD_CLR(requestP[i].conn_fd,&master);
								}
								// if no client inuse
								else{
									//read order detail from file and save to struct request
									pread(file_fd, &requestP[i].order, sizeof(Order), (nowid-902001)*sizeof(Order));
									
									sprintf(buf,"You can order %d adult mask(s) and %d children mask(s).\nPlease enter the mask type (adult or children) and number of mask you would like to order:\n",requestP[i].order.adultMask, requestP[i].order.childrenMask);
									// sprintf(buf,"Please enter the mask type (adult or children) and number of mask you would like to order:\n");
									//reply to client
									write(requestP[i].conn_fd, buf, strlen(buf));

									requestP[i].id = nowid;
									requestP[i].wait_for_write = 1;
									//set status and wait for next reply
								}
							}
						}
					}
					//for second request
					else{
						// read type and num from clients
						int ret = handle_read(&requestP[i]);				
						// type, num of request
						int amount;
						char mask_type[10];
						sscanf(requestP[i].buf, "%s %d", mask_type, &amount);
						// if type == "adult"
						if (!strcmp(mask_type,"adult")){
							//if not valid amount
							if(amount > requestP[i].order.adultMask || amount <= 0){
								sprintf(buf,"Operation failed.\n");
							}
							//if valid amount
							else{
								//change the detail of order
								requestP[i].order.adultMask-=amount;
								// write it to file
								pwrite(file_fd, &requestP[i].order, sizeof(requestP[i].order), (requestP[i].id - 902001)*sizeof(Order));
								//unlock file
								requestP[i].lock.l_type=F_UNLCK;
								fcntl(file_fd, F_SETLK, &requestP[i].lock);
								sprintf(buf,"Pre-order for %d successed, %d adult mask(s) ordered.\n",requestP[i].id,amount);
							}
						}
						//if type == 'children'
						else if (!strcmp(mask_type,"children")){
							//if not vaild amount
							if(amount > requestP[i].order.childrenMask || amount <= 0){
								sprintf(buf,"Operation failed.\n");
							}
							//if vaild amount
							else{
								requestP[i].order.childrenMask-=amount;
								sprintf(buf,"Pre-order for %d successed, %d children mask(s) ordered.\n",requestP[i].id,amount);
								pwrite(file_fd, &requestP[i].order, sizeof(requestP[i].order), (requestP[i].id - 902001)*sizeof(Order));
							}
						}
						//if not vaild type
						else{
							sprintf(buf,"Operation failed.\n");
						}
						//unlock the file lock
						requestP[i].lock.l_type=F_UNLCK;
						fcntl(file_fd, F_SETLK, &requestP[i].lock);

						write(requestP[i].conn_fd, buf, strlen(buf));
						close(requestP[i].conn_fd);
						free_request(&requestP[i]);
						FD_CLR(requestP[i].conn_fd,&master);
					}				
#endif
				}
			}
		}
		if(FD_ISSET(svr.listen_fd, &read_fds)){
			clilen = sizeof(cliaddr);
			new_conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
			if (new_conn_fd < 0) {
				if (errno == EINTR || errno == EAGAIN) continue;  // try again
				if (errno == ENFILE) {
					(void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
					continue;
				}
				ERR_EXIT("accept");
			}
			requestP[new_conn_fd].conn_fd = new_conn_fd;
			FD_SET(new_conn_fd,&master);
			strcpy(requestP[new_conn_fd].host, inet_ntoa(cliaddr.sin_addr));
			fprintf(stderr, "getting a new request... fd %d from %s\n", new_conn_fd, requestP[new_conn_fd].host);
			write(new_conn_fd, init, strlen(init));
		}
	}
	// free memory
	free(requestP);
	return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request* reqP) {
	reqP->conn_fd = -1;
	reqP->buf_len = 0;
	reqP->id = 0;
	reqP->wait_for_write = 0;
	memset(&reqP->order,0,sizeof(Order));
	memset(&reqP->lock,0,sizeof(struct flock));
}

static void free_request(request* reqP) {
	/*if (reqP->filename != NULL) {
		free(reqP->filename);
		reqP->filename = NULL;
	}*/
	init_request(reqP);
}

static void init_server(unsigned short port) {
	struct sockaddr_in servaddr;
	int tmp;

	gethostname(svr.hostname, sizeof(svr.hostname));
	svr.port = port;

	svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (svr.listen_fd < 0) ERR_EXIT("socket");

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	tmp = 1;
	if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
		ERR_EXIT("setsockopt");
	}
	if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		ERR_EXIT("bind");
	}
	if (listen(svr.listen_fd, 1024) < 0) {
		ERR_EXIT("listen");
	}

	// Get file descripter table size and initize request table
	maxfd = getdtablesize();
	requestP = (request*) malloc(sizeof(request) * maxfd);
	if (requestP == NULL) {
		ERR_EXIT("out of memory allocating all requests");
	}
	for (int i = 0; i < maxfd; i++) {
		init_request(&requestP[i]);
	}
	requestP[svr.listen_fd].conn_fd = svr.listen_fd;
	strcpy(requestP[svr.listen_fd].host, svr.hostname);

	return;
}
