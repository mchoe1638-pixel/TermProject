#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h> // malloc, free

#pragma comment(lib, "ws2_32.lib")

#include "game_shared.h"

// ======== 틱레이트 설정 ========
// 서버 게임 로직 틱레이트 (Hz)
#define SERVER_TICK_RATE        30                         // 30Hz
#define SERVER_TICK_INTERVAL_MS (1000 / SERVER_TICK_RATE)  // 약 33ms
#define SERVER_MAX_DT_SEC       0.1f                       // 한 번에 최대 0.1초만 진행

// 전역
static int g_running = 1;

unsigned __stdcall ClientThreadProc(void* arg);
unsigned __stdcall SessionLogicThreadProc(void* arg);

// 클라이언트 세션 구조체
typedef struct ClientSession {
    SOCKET sock;              // 이 세션의 클라이언트 소켓
    GameState state;          // 이 클라 전용 게임 상태
    CRITICAL_SECTION cs;      // 이 세션 상태 보호용
    volatile int running;     // 세션 종료 플래그
} ClientSession;

// 정확히 Len 바이트를 받을 때까지 반복
int recv_all(SOCKET s, char* buf, int len) {
    int recvd = 0;
    while (recvd < len) {
        int ret = recv(s, buf + recvd, len - recvd, 0);
        if (ret <= 0) return ret;
        recvd += ret;
    }
    return recvd;
}

int send_all(SOCKET s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int ret = send(s, buf + sent, len - sent, 0);
        if (ret <= 0) return ret;
        sent += ret;
    }
    return sent;
}

void err_quit(const char* msg)
{
    int err = WSAGetLastError();
    printf("%s (err=%d)\n", msg, err);
    exit(1);
}

// 클라이언트 입력 처리 (논블로킹)
int PollClientInput(ClientSession* sess)
{
    SOCKET s = sess->sock;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);

    TIMEVAL tv = { 0, 0 }; // 논블로킹
    int r = select(0, &rfds, NULL, NULL, &tv);
    if (r <= 0) {
        return 1; // 읽을 거 없음
    }

    if (!FD_ISSET(s, &rfds))
        return 1;

    PACKET_HEADER hdr;
    int ret = recv_all(s, (char*)&hdr, sizeof(hdr));
    if (ret <= 0) {
        printf("client disconnected while reading header (ret=%d)\n", ret);
        return 0;
    }

    if (hdr.Type == PKT_CL_PLACE_PLANT) {
        CL_PLACE_PLANT pkt;
        pkt.header = hdr;

        int bodySize = sizeof(CL_PLACE_PLANT) - sizeof(PACKET_HEADER);
        ret = recv_all(s, ((char*)&pkt) + sizeof(PACKET_HEADER), bodySize);
        if (ret <= 0) {
            printf("client disconnected while reading place_plant body (ret=%d)\n", ret);
            return 0;
        }

        printf("[RECV] CL_PLACE_PLANT row=%d col=%d type=%d\n",
            pkt.row, pkt.col, pkt.type);

        EnterCriticalSection(&sess->cs);
        PlacePlant(&sess->state, pkt.row, pkt.col, pkt.type);
        LeaveCriticalSection(&sess->cs);
    }
    else {
        printf("[WARN] Unknown packet type=%d, size=%d\n", hdr.Type, hdr.Size);

        // 헤더에 Size가 들어있으니까, 모르는 타입은 body만큼 그냥 버퍼에서 버림
        if (hdr.Size > 0) {
            char dumpBuf[512];
            int remain = hdr.Size;
            while (remain > 0) {
                int chunk = (remain > (int)sizeof(dumpBuf)) ? (int)sizeof(dumpBuf) : remain;
                ret = recv_all(s, dumpBuf, chunk);
                if (ret <= 0) {
                    printf("client disconnected while dropping unknown body (ret = %d)\n", ret);
                    return 0;
                }
                remain -= ret;
            }
        }
    }

    return 1;
}

int SendGameState(SOCKET s, const GameState* st)
{
    SV_GAME_STATE pkt;
    pkt.header.Type = PKT_SV_GAME_STATE;
    pkt.header.Size = sizeof(SV_GAME_STATE) - sizeof(PACKET_HEADER);
    pkt.state = *st;

    int ret = send_all(s, (const char*)&pkt, sizeof(pkt));
    if (ret <= 0) {
        printf("[ERR] send_all failed (ret=%d)\n", ret);
        return 0;
    }
    return 1;
}

// 세션 로직 스레드: 이 세션의 게임 상태를 틱레이트에 맞춰 업데이트
unsigned __stdcall SessionLogicThreadProc(void* arg)
{
    ClientSession* sess = (ClientSession*)arg;

    DWORD lastTick = GetTickCount();

    while (g_running && sess->running) {
        DWORD now = GetTickCount();
        DWORD diff = now - lastTick;

        if (diff < SERVER_TICK_INTERVAL_MS) {
            DWORD sleepMs = SERVER_TICK_INTERVAL_MS - diff;
            if (sleepMs > 1) Sleep(sleepMs);
            continue;
        }

        lastTick = now;

        float dt = diff / 1000.0f;
        if (dt > SERVER_MAX_DT_SEC)
            dt = SERVER_MAX_DT_SEC;

        EnterCriticalSection(&sess->cs);
        UpdateGameState(&sess->state, dt);
        LeaveCriticalSection(&sess->cs);
    }

    return 0;
}

// 클라이언트별 스레드: 입력 처리 + 상태 스냅샷 전송만 담당
unsigned __stdcall ClientThreadProc(void* arg)
{
    ClientSession* sess = (ClientSession*)arg;
    SOCKET cs = sess->sock;

    printf("Client session started.\n");

    while (g_running && sess->running) {
        // 1) 이 세션의 입력만 처리
        if (!PollClientInput(sess)) {
            printf("Client disconnected (input).\n");
            break;
        }

        // 2) 세션 상태 스냅샷
        GameState snapshot;
        EnterCriticalSection(&sess->cs);
        snapshot = sess->state;
        LeaveCriticalSection(&sess->cs);

        // 3) 이 세션의 상태만 전송
        if (!SendGameState(cs, &snapshot)) {
            printf("Client disconnected (send).\n");
            break;
        }

        Sleep(1000 / SERVER_TICK_RATE);
    }

    sess->running = 0;
    closesocket(cs);

    DeleteCriticalSection(&sess->cs);
    free(sess);

    return 0;
}

int main(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == INVALID_SOCKET) err_quit("socket()");

    // SO_REUSEADDR 설정
    {
        BOOL optval = TRUE;
        if (setsockopt(listenSock,
            SOL_SOCKET,
            SO_REUSEADDR,
            (const char*)&optval,
            sizeof(optval)) == SOCKET_ERROR) {
            printf("setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
        }
    }

    SOCKADDR_IN serveraddr;
    ZeroMemory(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(9000);

    if (bind(listenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR)
        err_quit("bind()");

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR)
        err_quit("listen()");

    printf("Server listening on port 9000...\n");

    while (g_running) {
        SOCKADDR_IN clientaddr;
        int addrlen = sizeof(clientaddr);
        SOCKET clientSock = accept(listenSock, (SOCKADDR*)&clientaddr, &addrlen);
        if (clientSock == INVALID_SOCKET) {
            printf("accept() failed.\n");
            continue;
        }

        // 세션 객체 동적 생성
        ClientSession* sess = (ClientSession*)malloc(sizeof(ClientSession));
        if (!sess) {
            printf("malloc ClientSession failed\n");
            closesocket(clientSock);
            continue;
        }

        sess->sock = clientSock;
        sess->running = 1;
        InitGameState(&sess->state);          // 각자 독립된 게임 시작
        InitializeCriticalSection(&sess->cs);

        // 세션 로직 스레드
        HANDLE hLogic = (HANDLE)_beginthreadex(
            NULL, 0, SessionLogicThreadProc, sess, 0, NULL);
        if (hLogic) CloseHandle(hLogic);

        // 세션 네트워크 스레드 (입력 + 상태 전송)
        HANDLE hClient = (HANDLE)_beginthreadex(
            NULL, 0, ClientThreadProc, sess, 0, NULL);
        if (hClient) CloseHandle(hClient);
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}

// 2025/11/19/최명규/서버-입력 수신 처리 PollClientInput()
// 2025/11/24/최명규/틱레이트 통합: SessionLogicThreadProc 도입, SERVER_TICK_RATE=30Hz
// 2025/12/03/최명규/식물 타입 전달, 세션별 GameState 난이도 적용
