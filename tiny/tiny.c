/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	doit(connfd);                                             // doit
	Close(connfd);                                            // 연결 닫고 다음 요청 기다리기
    }
}
/* $end tinymain */

/*
 * doit - HTTP request/response를 다룬다.
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* 헤더라인 읽기 */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {                     // Get 메소드 아니면 return
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }                                             
    read_requesthdrs(&rio);                              // 헤더 읽기

    /* Get 요청 파싱 */
    is_static = parse_uri(uri, filename, cgiargs);       // staic 인지 체크하기
    if (stat(filename, &sbuf) < 0) {                     // 요청 없으면
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }                                                    // 요청있을때

    if (is_static) { /* Serve static content */          
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // 유효성 검사 -> 읽기 권한이 있는지 체크
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
	    return;
	}
	serve_static(fd, filename, sbuf.st_size);        // static 보내기
    }
    else { /* Serve dynamic content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}
	serve_dynamic(fd, filename, cgiargs);            // 다이나믹 CGI 실행
    }
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* request 요청 읽기 */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* uri 파싱 */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;
    // 기본 가정 = 스트링 cgi-bin을 포함하는 모든 URI는 동적 컨텐츠를 요청한다고 생각

    if (!strstr(uri, "cgi-bin")) {  // 스태틱 컨텐츠 일때
	strcpy(cgiargs, ""); // cgi 초기화
	strcpy(filename, ".");  
	strcat(filename, uri); // endconvert 변환
	if (uri[strlen(uri)-1] == '/') //  '/'체크하기
	    strcat(filename, "home.html"); // 기본 파일 이름으로 변환
	return 1;
    }
    else {  /* 다이나믹 컨텐츠 일때 */                       
	ptr = index(uri, '?'); // 시작 부분 추출하기
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	}
	else 
	    strcpy(cgiargs, ""); // 끝 부분 초기화
	strcpy(filename, "."); // 처음꺼
	strcat(filename, uri); // 두번째 convert
	return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* response 보내기  */
    get_filetype(filename, filetype);       // 접미어 검사 해서 파일 타입 확인
    // 응답 줄과 응답 헤더를 보내야함
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    // 초기 버퍼 출력
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));       // 버퍼 안에 값 적용해버리기
    printf("Response headers:\n");
    printf("%s", buf);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    // 파일 디스크립터 번호 받기 및 Open()
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);// malloc과 같은 함수
    Close(srcfd);                           // 메모리로 매핑 한후 더 이상 식별자가 필요없으니 파일 닫는다.
    Rio_writen(fd, srcp, filesize);         // 파일을 클라리언트에게 전송하기
    Munmap(srcp, filesize);                 // free 느낌의 함수
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * 클라이언트에 동적 CGI 보여주기
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* HTTP reponse 첫 부분 반환 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* 책 12장에 간단한 설명이 있음 -> child 생성 */
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); // 환경변수를 요청 URI의 CGI 인자들로 초기화 한다.
	Dup2(fd, STDOUT_FILENO);         /* 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정(복사) */
	Execve(filename, emptylist, environ); /* 다시 CHI 프로그램을 로드하고 실행*/ //
    }
    Wait(NULL); /* 부모는 자식이 종료되어 정리하는 것을 기다리기 */ 
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */