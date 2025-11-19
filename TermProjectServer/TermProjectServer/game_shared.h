// game_shared.h
#pragma once

#include <windows.h>

// ======== 게임 상수 ========
#define MAX_ZOMBIES    64
#define MAX_ROWS       5
#define MAX_COLS       9

// 그리드 좌상단 위치 & 셀 크기
#define GRID_ORIGIN_X   50
#define GRID_ORIGIN_Y   80
#define CELL_WIDTH      80
#define CELL_HEIGHT     80

// 행/열 → 중앙 픽셀 좌표
#define COL_CENTER_X(c) (GRID_ORIGIN_X + (c) * CELL_WIDTH  + CELL_WIDTH  / 2.0f)
#define ROW_CENTER_Y(r) (GRID_ORIGIN_Y + (r) * CELL_HEIGHT + CELL_HEIGHT / 2.0f)

#define MAX_PROJECTILES 128
// 패킷 타입
#define PKT_CL_PLACE_PLANT  1    // 클라 -> 서버 : plant 설치
#define PKT_SV_GAME_STATE   2    // 서버 -> 클라 : 상태 전송

// ===================== 게임 엔티티 =====================

typedef struct Zombie {
    LONG x;   // 화면상의 X좌표
    LONG y;   // 화면상의 Y좌표
    int  hp;  // 나중에 쓸 체력
    int  alive; // 1 = 살아있음, 0 = 죽음
} Zombie;

// ===================== Plant 구조 =====================
typedef struct Plant {
    int type;      // 0=없음, 1=기본 공격 plant
    int alive;     // 1=살아있음
    int row;
    int col;
    float cooldown; // 발사 쿨다운(초)
    int hp;         // plant 체력 (좀비가 씹어먹을 때 감소)
} Plant;



// ===================== Projectile 구조 =====================
typedef struct Projectile {
    float x, y;
    float speed;       // 초당 이동 픽셀
    int alive;         // 1=살아있으면 이동/충돌 체크
    int damage;
} Projectile;

#define MAX_PROJECTILES 128

typedef struct GameState {
    int timeSec;
    int energy;

    int gameOver;   // 0=진행중, 1=패배

    Zombie zombies[MAX_ZOMBIES];
    int zombieCount;

    Plant plants[MAX_ROWS][MAX_COLS]; // grid 기반 plant

    Projectile projectiles[MAX_PROJECTILES];
    int projectileCount;
} GameState;

// ===================== 패킷 정의 =====================

#pragma pack(push, 1)

typedef struct PACKET_HEADER {
    int Type;   // PKT_*
    int Size;   // 헤더 제외 데이터 크기
} PACKET_HEADER;

// 클라 -> 서버 : plant 설치 요청
typedef struct CL_PLACE_PLANT {
    PACKET_HEADER header;
    int row;
    int col;
} CL_PLACE_PLANT;

// 서버 -> 클라 : 전체 게임 상태
typedef struct SV_GAME_STATE {
    PACKET_HEADER header;
    GameState state;
} SV_GAME_STATE;

// ===================== 게임 로직 함수 =====================

#ifdef __cplusplus
extern "C" {
#endif

    void InitGameState(GameState* st);
    void UpdateGameState(GameState* st, float dt);

#ifdef __cplusplus
}
#endif
// 2025/11/09/최명규/GameState 좀비 구조 추가
// 2025/11/16/최명규/GameState 식물, 투사체 구조 추가, GameState 구조 확장
// 2025/11/16/최명규/패킷 구조 정의
// 2025/11/19/최명규/식물, GameState확장 엔티티 추가 업데이트