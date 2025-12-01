#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

#include "game_shared.h"

// ======== 틱레이트 설정 ========
// 서버 게임 로직 틱레이트 (Hz)
#define SERVER_TICK_RATE        30                         // 30Hz
#define SERVER_TICK_INTERVAL_MS (1000 / SERVER_TICK_RATE)  // 약 33ms
#define SERVER_MAX_DT_SEC       0.1f                       // 한 번에 최대 0.1초만 진행 (갑자기 멈췄다 돌아올 때 폭주 방지)

// 전역
static GameState g_state;
static int g_running = 1;
CRITICAL_SECTION g_StateCS;   // GameState 보호용 CS

unsigned __stdcall ClientThreadProc(void* arg);
unsigned __stdcall GameLogicThreadProc(void* arg); // ★ 추가: 게임 로직 전용 스레드

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
int PollClientInput(SOCKET s)
{
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

        printf("[RECV] CL_PLACE_PLANT row=%d col=%d\n", pkt.row, pkt.col);

        EnterCriticalSection(&g_StateCS);
        PlacePlant(&g_state, pkt.row, pkt.col, 1);
        LeaveCriticalSection(&g_StateCS);
    }
    else {
        printf("[WARN] Unknown packet type=%d, size=%d\n", hdr.Type, hdr.Size);

        // 헤더에 Size가 들어있으니까, 모르는 타입은 body만큼 그냥 버퍼에서 버려도 됨
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

// ★ 추가: 서버 전체 게임 로직을 담당하는 전용 스레드
unsigned __stdcall GameLogicThreadProc(void* arg)
{
    (void)arg;

    DWORD lastTick = GetTickCount();

    while (g_running) {
        DWORD now = GetTickCount();
        DWORD diff = now - lastTick;

        // 아직 틱 간격이 안 지났으면 조금 더 쉰다
        if (diff < SERVER_TICK_INTERVAL_MS) {
            DWORD sleepMs = SERVER_TICK_INTERVAL_MS - diff;
            if (sleepMs > 1) Sleep(sleepMs);
            continue;
        }

        lastTick = now;

        // ms → 초 단위로 변환
        float dt = diff / 1000.0f;

        // 갑자기 멈췄다 돌아오면 diff가 크게 튈 수 있으니 제한
        if (dt > SERVER_MAX_DT_SEC)
            dt = SERVER_MAX_DT_SEC;

        // ★ 여기서만 GameState를 진행시킨다
        EnterCriticalSection(&g_StateCS);
        UpdateGameState(&g_state, dt);
        LeaveCriticalSection(&g_StateCS);
    }

    return 0;
}

// 클라이언트별 스레드: 입력 처리 + 상태 스냅샷 전송만 담당
unsigned __stdcall ClientThreadProc(void* arg)
{
    SOCKET cs = (SOCKET)arg;
    printf("Client connected.\n");

    while (g_running) {
        // 1) 클라이언트 입력 처리 (논블로킹)
        if (!PollClientInput(cs)) {
            printf("Client disconnected (input).\n");
            break;
        }

        // 2) 현재 GameState 스냅샷 복사
        GameState snapshot;
        EnterCriticalSection(&g_StateCS);
        snapshot = g_state;
        LeaveCriticalSection(&g_StateCS);

        // 3) 스냅샷 전송
        if (!SendGameState(cs, &snapshot)) {
            printf("Client disconnected (send).\n");
            break;
        }

        // 너무 자주 보내면 네트워크/CPU 낭비니까 틱레이트에 맞춰서 살짝 쉰다
        Sleep(1000 / SERVER_TICK_RATE); // 30Hz면 대략 33ms
    }

    closesocket(cs);
    return 0;
}

int main(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    InitGameState(&g_state);
    InitializeCriticalSection(&g_StateCS);

    // ★ 게임 로직 전용 스레드 시작 (서버 전체 틱레이트 컨트롤)
    HANDLE hLogicThread = (HANDLE)_beginthreadex(
        NULL, 0, GameLogicThreadProc, NULL, 0, NULL);
    if (hLogicThread)
        CloseHandle(hLogicThread);

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

        HANDLE hThread = (HANDLE)_beginthreadex(
            NULL, 0, ClientThreadProc, (void*)clientSock, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
    }

    closesocket(listenSock);
    DeleteCriticalSection(&g_StateCS);
    WSACleanup();
    return 0;
}

// 2025/11/19/최명규/서버-입력 수신 처리 PollClientInput()
// 2025/11/24/최명규/틱레이트 통합: GameLogicThread 도입, SERVER_TICK_RATE=30Hz
