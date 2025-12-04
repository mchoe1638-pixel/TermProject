#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define DEFAULT_SERVER_IP "192.168.72.211"
#define DEFAULT_SERVER_PORT 9000

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
    SCENE_PLAY = 1,    // 실제 서버-클라 게임 화면
} CLIENT_SCENE;

static CLIENT_SCENE g_Scene = SCENE_TITLE;
static HBITMAP g_hTitleBitmap = NULL;

static RECT g_TitleStartButton;     // 시작 버튼 영역
static RECT g_TitleQuitButton;      // 종료 버튼 영역

// 전역
HWND   g_hWnd = NULL;
SOCKET g_serverSock = INVALID_SOCKET;
HANDLE g_hRecvThread = NULL;
volatile int g_netRunning = 0;
GameState g_state;   // 서버에서 받은 상태
CRITICAL_SECTION g_StateCS;  // GameState 보호용

// 현재 선택된 식물 타입 (1=단거리, 2=장거리, 3=근거리)
static int g_SelectedPlantType = 1;

// Rendering에 사용할 Buffer (지금은 단순 예시)
typedef struct RenderBuffer {
    int numZombies;
} RenderBuffer;
RenderBuffer g_RenderBuffer; // 공유 렌더 버퍼
CRITICAL_SECTION g_RenderCS;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
unsigned __stdcall RecvThreadProc(void* arg);

// 마우스 좌표 → 그리드(row, col)
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
void SendPlacePlant(int row, int col, int type)
{
    if (g_serverSock == INVALID_SOCKET)
        return;

    CL_PLACE_PLANT pkt;
    pkt.header.Type = PKT_CL_PLACE_PLANT;
    pkt.header.Size = sizeof(CL_PLACE_PLANT) - sizeof(PACKET_HEADER);
    pkt.row = row;
    pkt.col = col;
    pkt.type = type;

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
    serveraddr.sin_addr.s_addr = inet_addr(ip);
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

        if (pkt.header.Type != PKT_SV_GAME_STATE) {
            continue;
        }

        EnterCriticalSection(&g_StateCS);
        g_state = pkt.state;
        LeaveCriticalSection(&g_StateCS);

        if (g_hWnd) {
            InvalidateRect(g_hWnd, NULL, FALSE);
        }
    }

    return 0;
}

void RenderScene(HDC hdc, const GameState* st)
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);

    // 배경
    HBRUSH hBG = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, hBG);
    DeleteObject(hBG);

    // 디버그 텍스트: 시간, 웨이브, 좀비 수, 선택 식물 타입
    wchar_t buf[256];
    wsprintfW(
        buf,
        L"time=%d  wave=%d/%d  zombies=%d  plantType=%d",
        st->timeSec,
        st->waveIndex + 1,
        st->totalWaves,
        st->zombieCount,
        g_SelectedPlantType
    );
    TextOutW(hdc, 10, 10, buf, lstrlenW(buf));

    // 그리드 선
    HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCPenColor(hdc, RGB(200, 200, 200));

    for (int c = 0; c <= MAX_COLS; ++c) {
        int x = GRID_ORIGIN_X + c * CELL_WIDTH;
        MoveToEx(hdc, x, GRID_ORIGIN_Y, NULL);
        LineTo(hdc, x, GRID_ORIGIN_Y + MAX_ROWS * CELL_HEIGHT);
    }

    for (int r = 0; r <= MAX_ROWS; ++r) {
        int y = GRID_ORIGIN_Y + r * CELL_HEIGHT;
        MoveToEx(hdc, GRID_ORIGIN_X, y, NULL);
        LineTo(hdc, GRID_ORIGIN_X + MAX_COLS * CELL_HEIGHT, y);
    }

    // Plant 그리기 (타입별 색상 약간 다르게)
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            const Plant* p = &st->plants[r][c];
            if (!p->alive) continue;

            int centerX = (int)COL_CENTER_X(c);
            int centerY = (int)ROW_CENTER_Y(r);
            int half = 25;

            COLORREF penColor = RGB(0, 120, 0);
            COLORREF brushColor = RGB(0, 200, 0);

            if (p->type == 2) {
                penColor = RGB(0, 80, 160);   // 장거리
                brushColor = RGB(80, 180, 255);
            }
            else if (p->type == 3) {
                penColor = RGB(160, 80, 0);   // 근거리
                brushColor = RGB(220, 150, 80);
            }

            SetDCPenColor(hdc, penColor);
            SetDCBrushColor(hdc, brushColor);
            Rectangle(hdc,
                centerX - half, centerY - half,
                centerX + half, centerY + half);
        }
    }

    // 좀비 그리기
    HPEN   hOldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(DC_BRUSH));

    for (int i = 0; i < st->zombieCount; ++i) {
        const Zombie* z = &st->zombies[i];
        if (!z->alive) continue;

        int x = (int)z->x;
        int y = (int)z->y;
        int half = 20;

        SetDCPenColor(hdc, RGB(255, 0, 0));
        SetDCBrushColor(hdc, RGB(255, 200 - (i * 15) % 150, 200));

        Rectangle(hdc, x - half, y - half, x + half, y + half);
    }

    // Projectile 그리기
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

    HBRUSH hBG = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &rc, hBG);
    DeleteObject(hBG);

    if (g_hTitleBitmap) {
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, g_hTitleBitmap);

        BITMAP bm;
        GetObject(g_hTitleBitmap, sizeof(bm), &bm);

        StretchBlt(
            hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
            memDC, 0, 0, bm.bmWidth, bm.bmHeight,
            SRCCOPY
        );

        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    const wchar_t* title = L"Plant Defense (Network)";
    TextOutW(hdc, 50, 50, title, lstrlenW(title));

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
        g_Scene = SCENE_TITLE;

        RECT rc;
        GetClientRect(hWnd, &rc);
        int cx = (rc.right - rc.left);
        int cy = (rc.bottom - rc.top);

        int btnW = 200;
        int btnH = 60;
        int centerX = cx / 2;

        g_TitleStartButton.left = centerX - btnW / 2;
        g_TitleStartButton.right = centerX + btnW / 2;
        g_TitleStartButton.top = cy / 2 - 40;
        g_TitleStartButton.bottom = g_TitleStartButton.top + btnH;

        g_TitleQuitButton.left = centerX - btnW / 2;
        g_TitleQuitButton.right = centerX + btnW / 2;
        g_TitleQuitButton.top = g_TitleStartButton.bottom + 20;
        g_TitleQuitButton.bottom = g_TitleQuitButton.top + btnH;

        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        if (g_Scene == SCENE_TITLE) {
            RenderTitleScreen(hdc);
        }
        else {
            GameState local;
            EnterCriticalSection(&g_StateCS);
            local = g_state;
            LeaveCriticalSection(&g_StateCS);

            RenderScene(hdc, &local);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
    {
        // 식물 타입 선택: 1, 2, 3
        if (wParam == '1') {
            g_SelectedPlantType = 1;
        }
        else if (wParam == '2') {
            g_SelectedPlantType = 2;
        }
        else if (wParam == '3') {
            g_SelectedPlantType = 3;
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (g_Scene == SCENE_TITLE) {
            if (PtInRect(&g_TitleStartButton, pt)) {
                g_Scene = SCENE_PLAY;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            if (PtInRect(&g_TitleQuitButton, pt)) {
                PostMessage(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }

            return 0;
        }

        if (g_Scene == SCENE_PLAY) {
            int row, col;
            if (ScreenToGrid(x, y, &row, &col)) {
                SendPlacePlant(row, col, g_SelectedPlantType);
            }
            return 0;
        }

        return 0;
    }
    case WM_DESTROY:
        if (g_hTitleBitmap) {
            DeleteObject(g_hTitleBitmap);
            g_hTitleBitmap = NULL;
        }

        g_netRunning = 0;

        if (g_serverSock != INVALID_SOCKET) {
            shutdown(g_serverSock, SD_BOTH);
        }

        if (g_hRecvThread) {
            WaitForSingleObject(g_hRecvThread, INFINITE);
            CloseHandle(g_hRecvThread);
            g_hRecvThread = NULL;
        }

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
    InitializeCriticalSection(&g_StateCS);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 서버 접속 (IP는 네트워크 환경에 맞게 수정)
    if (!ConnectToServer(DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT)) {
        MessageBoxW(NULL, L"서버 연결 실패", L"Error", MB_OK);
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&g_StateCS);
    return (int)msg.wParam;
}
// 2025/11/15/최명규/LogicTick에서 좀비 움직임
// 2025/11/19/최명규/그리드, 식물, 그리드 내에서 마우스 클릭 처리
// 2025/12/03/최명규/식물 타입 선택(1,2,3), HUD 표시, CL_PLACE_PLANT.type 전송
