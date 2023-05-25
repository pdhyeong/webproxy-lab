#include <stdio.h>
#include "csapp.h"

/* 권장되는 최대 캐시 및 오브젝트 크기 */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 스타일 점수를 잃지 않으셔도 됩니다. 아래의 긴 줄을 코드에 포함시키는 것은 괜찮습니다. */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *new_version = "HTTP/1.0";

/* 함수 프로토타입 */
void *thread_func(void *arg);
void handle_request(int proxy_connfd);
void send_request(int p_clientfd, char *method, char *uri_ptos, char *host);
void handle_response(int p_connfd, int p_clientfd);
int parse_uri(char *uri, char *uri_ptos, char *host, char *port);

int main(int argc, char **argv)
{
  int listenfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* 명령행 인수 확인 */
  if (argc != 2)
  {
    fprintf(stderr, "사용법: %s <포트>\n", argv[0]);
    exit(1);
  }
  /* 지정된 포트에 대한 수신 소켓 생성 */
  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    int *connfdp = malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* 각 클라이언트 연결마다 새로운 스레드 생성 */
    pthread_create(&tid, NULL, thread_func, connfdp);
  }
  return 0;
}

void *thread_func(void *arg)
{
  int p_connfd = *((int *)arg);
  pthread_detach(pthread_self());
  Free(arg);

  handle_request(p_connfd);
  Close(p_connfd);

  return NULL;
}

/*
파싱 전 (클라이언트로부터 받은 요청 라인)
=> GET http://www.google.com:80/index.html HTTP/1.1
​
파싱 결과
=> host = www.google.com
=> port = 80
=> uri_ptos = /index.html
​
파싱 후 (서버로 보낼 요청 라인)
=> GET /index.html HTTP/1.0
*/

void handle_request(int proxy_connfd)
{
  int server_connfd;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char transformed_uri[MAXLINE];
  rio_t rio;

  /* 클라이언트로부터 요청 라인과 헤더 읽기 */
  Rio_readinitb(&rio, proxy_connfd); // rio 버퍼를 프록시의 연결 파일 디스크립터(proxy_connfd)와 연결
  Rio_readlineb(&rio, buf, MAXLINE); // rio(프록시의 연결 파일 디스크립터)에서 한 줄(요청 라인)을 읽고 buf에 저장
  printf("프록시로부터의 요청 헤더:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // buf에서 세 개의 문자열을 읽어서 각각 method, uri, version에 저장

  /* GET 요청에서 URI 파싱 */
  parse_uri(uri, transformed_uri, host, port);

  server_connfd = Open_clientfd(host, port);                  // 서버에 연결하고 서버의 연결 파일 디스크립터(server_connfd)를 가져옴
  send_request(server_connfd, method, transformed_uri, host); // 서버의 연결 파일 디스크립터에 요청 헤더를 보내고 동시에 서버의 연결 파일 디스크립터에도 씀
  handle_response(proxy_connfd, server_connfd);
  Close(server_connfd); // 서버 연결 파일 디스크립터 닫기
}

/* send_request: 프록시 => 서버 */
void send_request(int p_clientfd, char *method, char *uri_ptos, char *host)
{
  char buf[MAXLINE];
  printf("서버로 보내는 요청 헤더: \n");
  printf("%s %s %s\n", method, uri_ptos, new_version);

  /* 요청 헤더 읽기 */
  sprintf(buf, "GET %s %s\r\n", uri_ptos, new_version);   // GET /index.html HTTP/1.0
  sprintf(buf, "%sHost: %s\r\n", buf, host);              // Host: www.google.com
  sprintf(buf, "%s%s", buf, user_agent_hdr);              // User-Agent: ~(bla bla)
  sprintf(buf, "%sConnections: close\r\n", buf);          // Connections: close
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf); // Proxy-Connection: close

  /* Rio_writen: buf에서 p_clientfd로 strlen(buf)바이트 전송 */
  Rio_writen(p_clientfd, buf, (size_t)strlen(buf)); // => 요청을 보내는 행위 자체
}

/* handle_response: 서버 => 프록시 */
void handle_response(int p_connfd, int p_clientfd)
{
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, p_clientfd);           //
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE); // 최대 MAXLINE까지 안전하게 모두 읽음
  Rio_writen(p_connfd, buf, n);
}
/* parse_uri: (클라이언트로부터 받은) GET 요청에서 URI 파싱, 서버로의 GET 요청을 위해 필요 */
int parse_uri(char *uri, char *uri_ptos, char *host, char *port)
{
  char *ptr = strstr(uri, "://");
  if (!ptr)
    return -1;
  ptr += 3;
  sscanf(ptr, "%[^:/]:%[^/]%s", host, port, uri_ptos);
  if (strcmp(port, "") == 0)
    strcpy(port, "80");
  if (strcmp(uri_ptos, "") == 0)
    strcpy(uri_ptos, "/");
  return 0;
}