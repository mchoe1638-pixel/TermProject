#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

#include "game_shared.h"

// 전역
static GameState g_state;
static int g_running = 1;

unsigned __stdcall ClientThreadProc(void* arg);

void err_quit(const char* msg)
{
    int err = WSAGetLastError();
    printf("%s (err=%d)\n", msg, err);
    exit(1);
}

int SendGameState(SOCKET s)
{
    SV_GAME_STATE pkt;
    pkt.header.Type = 2; // SV_GAME_STATE
    pkt.header.Size = sizeof(SV_GAME_STATE) - sizeof(PACKET_HEADER);
    pkt.state = g_state;

    int toSend = sizeof(pkt);
    char* buf = (char*)&pkt;
    int sent = 0;

    while (sent < toSend) {
        int ret = send(s, buf + sent, toSend - sent, 0);
        if (ret <= 0) return 0;
        sent += ret;
    }
    return 1;
}

unsigned __stdcall ClientThreadProc(void* arg)
{
    SOCKET cs = (SOCKET)arg;
    printf("Client connected.\n");

    DWORD lastTick = GetTickCount();

    while (g_running) {
        DWORD now = GetTickCount();
        float dt = (now - lastTick) / 1000.0f;
        if (dt < 0.05f) {
            Sleep(10);
            continue;
        }
        lastTick = now;

        // 게임 상태 업데이트 (지금은 timeSec만 증가)
        UpdateGameState(&g_state, dt);

        // 상태 전송
        if (!SendGameState(cs)) {
            printf("Client disconnected.\n");
            break;
        }
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
    

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == INVALID_SOCKET) err_quit("socket()");

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
        CloseHandle(hThread);
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}