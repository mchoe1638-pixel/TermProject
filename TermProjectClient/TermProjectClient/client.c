// client_main.c
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

#include "game_shared.h"

// 전역
HWND   g_hWnd = NULL;
SOCKET g_serverSock = INVALID_SOCKET;
HANDLE g_hRecvThread = NULL;
volatile int g_netRunning = 0;
GameState g_state;   // 서버에서 받은 상태

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
unsigned __stdcall RecvThreadProc(void* arg);

int ConnectToServer(const char* ip, unsigned short port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        MessageBoxW(NULL, L"WSAStartup 실패", L"Error", MB_OK);
        return 0;
    }

    g_serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_serverSock == INVALID_SOCKET) {
        MessageBoxW(NULL, L"socket() 실패", L"Error", MB_OK);
        return 0;
    }

    SOCKADDR_IN serveraddr;
    ZeroMemory(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 로컬호스트
    serveraddr.sin_port = htons(port);

    if (connect(g_serverSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR) {
        MessageBoxW(NULL, L"connect() 실패", L"Error", MB_OK);
        closesocket(g_serverSock);
        g_serverSock = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    InitGameState(&g_state);
    g_netRunning = 1;

    g_hRecvThread = (HANDLE)_beginthreadex(
        NULL, 0, RecvThreadProc, NULL, 0, NULL);
    if (g_hRecvThread)
        CloseHandle(g_hRecvThread);

    return 1;
}

unsigned __stdcall RecvThreadProc(void* arg)
{
    (void)arg;

    while (g_netRunning) {
        SV_GAME_STATE pkt;
        int toRecv = sizeof(pkt);
        char* buf = (char*)&pkt;
        int recvd = 0;

        while (recvd < toRecv) {
            int ret = recv(g_serverSock, buf + recvd, toRecv - recvd, 0);
            if (ret <= 0) {
                g_netRunning = 0;
                break;
            }
            recvd += ret;
        }

        if (!g_netRunning)
            break;

        if (pkt.header.Type != 2) {
            continue;
        }

        // 그냥 통째로 복사
        g_state = pkt.state;

        // 화면 다시 그리기
        if (g_hWnd) {
            InvalidateRect(g_hWnd, NULL, FALSE);
        }
    }

    if (g_serverSock != INVALID_SOCKET) {
        closesocket(g_serverSock);
        g_serverSock = INVALID_SOCKET;
    }
    WSACleanup();
    return 0;
}

void RenderScene(HDC hdc, const GameState* st)
{
    // ----- 0. 클라 창 크기 -----
    RECT rc;
    GetClientRect(g_hWnd, &rc);

    // 배경 흰색으로 깔기
    HBRUSH hBG = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, hBG);
    DeleteObject(hBG);

    // ----- 1. 디버그 텍스트 -----
    wchar_t buf[128];
    wsprintfW(
        buf,
        L"time=%d  zombies=%d",
        st->timeSec,
        st->zombieCount
    );
    TextOutW(hdc, 10, 10, buf, lstrlenW(buf));


    // ----- 2. 좀비 전체 그리기 -----
    HPEN   hOldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(DC_BRUSH));

    for (int i = 0; i < st->zombieCount; ++i) {
        const Zombie* z = &st->zombies[i];
        if (!z->alive) continue;

        int x = (int)z->x;
        int y = (int)z->y;
        int half = 20;     // 박스 반지름

        // 인덱스별로 색 조금씩 바꿔보기 (구분용)
        SetDCPenColor(hdc, RGB(255, 0, 0));
        SetDCBrushColor(hdc, RGB(255, 200 - (i * 15) % 150, 200));

        Rectangle(hdc, x - half, y - half, x + half, y + half);
    }

    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // 1) 서버에서 받은 상태를 로컬로 복사
        GameState local = g_state;   // 스레드 세이프는 아니지만 일단 스냅샷용

        // 2) 그 로컬 상태 기반으로 그리기
        RenderScene(hdc, &local);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        g_netRunning = 0;
        if (g_serverSock != INVALID_SOCKET) {
            closesocket(g_serverSock);
            g_serverSock = INVALID_SOCKET;
        }
        WSACleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    const wchar_t CLASS_NAME[] = L"NetClientWindow";

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"RegisterClass 실패", L"Error", MB_OK);
        return 0;
    }

    HWND hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"PlantDefense Client",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hWnd) {
        MessageBoxW(NULL, L"CreateWindow 실패", L"Error", MB_OK);
        return 0;
    }

    g_hWnd = hWnd;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 서버 접속 (로컬 127.0.0.1:9000)
    if (!ConnectToServer("127.0.0.1", 9000)) {
        MessageBoxW(NULL, L"서버 연결 실패", L"Error", MB_OK);
        // 서버 없어도 그냥 창은 뜨게 두고 싶으면 return 0 빼도 됨
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
// 2025/11/15/최명규/LogicTick에서 좀비 움직임-------------------------------------