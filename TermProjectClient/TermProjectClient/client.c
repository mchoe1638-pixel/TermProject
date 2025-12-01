// client_main.c
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <windowsx.h> 
#include <process.h>
#include <stdio.h>
#pragma comment(lib, "ws2_32.lib")

#include "game_shared.h"
#include "resource.h"

typedef enum {
    SCENE_TITLE = 0,   // 시작화면 (비트맵)
    SCENE_PLAY = 1,   // 실제 서버-클라 게임 화면
} CLIENT_SCENE;
static CLIENT_SCENE g_Scene = SCENE_TITLE;
static HBITMAP g_hTitleBitmap = NULL;
static RECT g_TitleStartRect = { 500, 485, 875, 570 };

static BOOL g_GameWin = FALSE;      // 나중에 결과 화면 쓸 때 쓸 플래그(지금은 안 써도 됨)
static RECT g_TitleStartButton;     // 시작 버튼 영역
static RECT g_TitleQuitButton;      // 종료 버튼 영역

// 전역
HWND   g_hWnd = NULL;
SOCKET g_serverSock = INVALID_SOCKET;
HANDLE g_hRecvThread = NULL;
volatile int g_netRunning = 0;
GameState g_state;   // 서버에서 받은 상태
CRITICAL_SECTION g_StateCS;  // 추가: GameState 보호용

// Rendering에 사용할 Buffer
typedef struct RenderBuffer {
    int numZombies;
    // 좀비, 식물, 투사체 등등 서버 상태를 그리기 좋게 변환한 데이터들
} RenderBuffer;
RenderBuffer g_RenderBuffer; // 공유 렌더 버퍼
CRITICAL_SECTION g_RenderCS; // 이걸로 보호

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
unsigned __stdcall RecvThreadProc(void* arg);

// 마우스 좌표 변환 함수(row, col)
int ScreenToGrid(int x, int y, int* outRow, int* outCol)
{
    x -= GRID_ORIGIN_X;
    y -= GRID_ORIGIN_Y;

    if (x < 0 || y < 0) return 0;

    int col = x / CELL_WIDTH;
    int row = y / CELL_HEIGHT;

    if (row < 0 || row >= MAX_ROWS) return 0;
    if (col < 0 || col >= MAX_COLS) return 0;

    *outRow = row;
    *outCol = col;
    return 1;
}

// 클라이언트에서 서버로 패킷을 전송하는 함수
void SendPlacePlant(int row, int col)
{
    if (g_serverSock == INVALID_SOCKET)
        return;

    CL_PLACE_PLANT pkt;
    pkt.header.Type = PKT_CL_PLACE_PLANT;
    pkt.header.Size = sizeof(CL_PLACE_PLANT) - sizeof(PACKET_HEADER);
    pkt.row = row;
    pkt.col = col;

    int toSend = sizeof(pkt);
    char* p = (char*)&pkt;
    int sent = 0;

    while (sent < toSend) {
        int ret = send(g_serverSock, p + sent, toSend - sent, 0);
        if (ret <= 0) {
            // 연결 끊김
            g_netRunning = 0;
            return;
        }
        sent += ret;
    }
}

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

        EnterCriticalSection(&g_StateCS);
        // 그냥 통째로 복사
        g_state = pkt.state;
        LeaveCriticalSection(&g_StateCS);


        // 화면 다시 그리기
        if (g_hWnd) {
            InvalidateRect(g_hWnd, NULL, FALSE);
        }
    }

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

    // ----- 그리드 선 그리기 -----
    HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCPenColor(hdc, RGB(200, 200, 200));

    // 세로선
    for (int c = 0; c <= MAX_COLS; ++c) {
        int x = GRID_ORIGIN_X + c * CELL_WIDTH;
        MoveToEx(hdc, x, GRID_ORIGIN_Y, NULL);
        LineTo(hdc, x, GRID_ORIGIN_Y + MAX_ROWS * CELL_HEIGHT);
    }

    // 가로선
    for (int r = 0; r <= MAX_ROWS; ++r) {
        int y = GRID_ORIGIN_Y + r * CELL_HEIGHT;
        MoveToEx(hdc, GRID_ORIGIN_X, y, NULL);
        LineTo(hdc, GRID_ORIGIN_X + MAX_COLS * CELL_WIDTH, y);
    }

    // ----- Plant 그리기 -----
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            const Plant* p = &st->plants[r][c];
            if (!p->alive) continue;

            int centerX = (int)COL_CENTER_X(c);
            int centerY = (int)ROW_CENTER_Y(r);
            int half = 25;

            SetDCPenColor(hdc, RGB(0, 120, 0));
            SetDCBrushColor(hdc, RGB(0, 200, 0));
            Rectangle(hdc,
                centerX - half, centerY - half,
                centerX + half, centerY + half);
        }
    }

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

    // ----- Projectile 그리기 -----
    for (int i = 0; i < st->projectileCount; ++i) {
        const Projectile* p = &st->projectiles[i];
        if (!p->alive) continue;

        int x = (int)p->x;
        int y = (int)p->y;

        SetDCPenColor(hdc, RGB(255, 255, 0));
        SetDCBrushColor(hdc, RGB(255, 255, 100));

        Ellipse(hdc, x - 5, y - 5, x + 5, y + 5);
    }


    // 승리/패배 텍스트
    SetBkMode(hdc, TRANSPARENT);

    if (st->gameResult == 2) {          // 패배
        SetTextColor(hdc, RGB(255, 0, 0));
        const wchar_t* msg = L"GAME OVER";
        TextOutW(hdc, 200, 50, msg, lstrlenW(msg));
    }
    else if (st->gameResult == 1) {     // 승리
        SetTextColor(hdc, RGB(0, 0, 255));
        const wchar_t* msg = L"YOU WIN!";
        TextOutW(hdc, 200, 50, msg, lstrlenW(msg));
    }

    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
}

void RenderTitleScreen(HDC hdc)
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);

    // 배경 색
    HBRUSH hBG = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &rc, hBG);
    DeleteObject(hBG);

    // (옵션) 비트맵 배경 사용
    if (g_hTitleBitmap) {
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, g_hTitleBitmap);

        BITMAP bm;
        GetObject(g_hTitleBitmap, sizeof(bm), &bm);

        // 윈도우 크기에 맞게 스트레치
        StretchBlt(
            hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
            memDC, 0, 0, bm.bmWidth, bm.bmHeight,
            SRCCOPY
        );

        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }

    // 타이틀 텍스트
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    const wchar_t* title = L"Plant Defense (Network)";
    TextOutW(hdc, 50, 50, title, lstrlenW(title));

    // 버튼 그리기 (START / QUIT)
    HBRUSH hStartBrush = CreateSolidBrush(RGB(0, 150, 0));
    HBRUSH hQuitBrush = CreateSolidBrush(RGB(150, 0, 0));
    HBRUSH oldBrush;

    // START 버튼
    oldBrush = (HBRUSH)SelectObject(hdc, hStartBrush);
    Rectangle(hdc,
        g_TitleStartButton.left,
        g_TitleStartButton.top,
        g_TitleStartButton.right,
        g_TitleStartButton.bottom);

    const wchar_t* startText = L"START";
    TextOutW(
        hdc,
        g_TitleStartButton.left + 60,
        g_TitleStartButton.top + 20,
        startText,
        lstrlenW(startText)
    );

    // QUIT 버튼
    SelectObject(hdc, hQuitBrush);
    Rectangle(hdc,
        g_TitleQuitButton.left,
        g_TitleQuitButton.top,
        g_TitleQuitButton.right,
        g_TitleQuitButton.bottom);

    const wchar_t* quitText = L"QUIT";
    TextOutW(
        hdc,
        g_TitleQuitButton.left + 70,
        g_TitleQuitButton.top + 20,
        quitText,
        lstrlenW(quitText)
    );

    SelectObject(hdc, oldBrush);
    DeleteObject(hStartBrush);
    DeleteObject(hQuitBrush);
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
    {
        // 시작 화면부터 표시
        g_Scene = SCENE_TITLE;
        g_GameWin = FALSE;

        // 윈도우 크기 기준으로 버튼 위치 대충 가운데 쯤 배치
        RECT rc;
        GetClientRect(hWnd, &rc);
        int cx = (rc.right - rc.left);
        int cy = (rc.bottom - rc.top);

        int btnW = 200;
        int btnH = 60;
        int centerX = cx / 2;

        // START 버튼
        g_TitleStartButton.left = centerX - btnW / 2;
        g_TitleStartButton.right = centerX + btnW / 2;
        g_TitleStartButton.top = cy / 2 - 40;
        g_TitleStartButton.bottom = g_TitleStartButton.top + btnH;

        // QUIT 버튼
        g_TitleQuitButton.left = centerX - btnW / 2;
        g_TitleQuitButton.right = centerX + btnW / 2;
        g_TitleQuitButton.top = g_TitleStartButton.bottom + 20;
        g_TitleQuitButton.bottom = g_TitleQuitButton.top + btnH;

        // (옵션) 비트맵 로드 하고 싶으면 리소스 추가 후 이거 살리면 됨
        // g_hTitleBitmap = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP1));

        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        if (g_Scene == SCENE_TITLE) {
            // 1) 타이틀 화면
            RenderTitleScreen(hdc);
        }
        else {
            // 2) 인게임 / 결과 화면 → 서버 상태 기반으로 그리기
            GameState local;

            EnterCriticalSection(&g_StateCS);
            local = g_state;      // 스냅샷 복사
            LeaveCriticalSection(&g_StateCS);

            RenderScene(hdc, &local);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    // WndProc에서 마우스 클릭 처리
    // 그리드 안을 클릭하면 row, col계산해서 서버에 CL_PLACE_PLANT전송
    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        // 1) 타이틀 씬일 때: 버튼 클릭 처리
        if (g_Scene == SCENE_TITLE) {

            if (PtInRect(&g_TitleStartButton, pt)) {
                // START 버튼 → 게임 시작
                g_Scene = SCENE_PLAY;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            if (PtInRect(&g_TitleQuitButton, pt)) {
                // QUIT 버튼 → 창 닫기
                PostMessage(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }

            // 타이틀 화면에서 다른 데 클릭하면 아무 것도 안 함
            return 0;
        }

        // 2) 게임 플레이 씬일 때: 기존 심기 로직
        if (g_Scene == SCENE_PLAY) {
            int row, col;
            if (ScreenToGrid(x, y, &row, &col)) {
                SendPlacePlant(row, col);
            }
            return 0;
        }

        // 3) 나중에 SCENE_RESULT에서 “다시하기” 같은 것 넣을 거면 여기서 분기 추가하면 됨
        return 0;
    }
    case WM_DESTROY:
        if (g_hTitleBitmap) {
            DeleteObject(g_hTitleBitmap);
            g_hTitleBitmap = NULL;
        }
        // 1) RecvThread 루프 종료 플래그
        g_netRunning = 0;

        // 2) 소켓 송수신 중단 (recv 깨우기용)
        if (g_serverSock != INVALID_SOCKET) {
            shutdown(g_serverSock, SD_BOTH);  // 송수신 다 끊겠다고 서버/로컬에 알림
        }

        // 3) RecvThread가 끝날 때까지 대기
        if (g_hRecvThread) {
            WaitForSingleObject(g_hRecvThread, INFINITE);
            CloseHandle(g_hRecvThread);
            g_hRecvThread = NULL;
        }

        // 4) 이제 진짜 소켓 닫기
        if (g_serverSock != INVALID_SOCKET) {
            closesocket(g_serverSock);
            g_serverSock = INVALID_SOCKET;
        }

        // 5) WSA 정리 (한 번만)
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
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 800,
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
    // 여기: CS 초기화
    InitializeCriticalSection(&g_StateCS);

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
    DeleteCriticalSection(&g_StateCS);       // 종료 시 정리
    return (int)msg.wParam;
}
// 2025/11/15/최명규/LogicTick에서 좀비 움직임
// 2025/11/19/최명규/그리드, 식물, 그리드 내에서 마우스 클릭 처리