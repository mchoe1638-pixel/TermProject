#pragma once
#include <windows.h>

// ===== 게임 엔티티 구조 (나중에 네 기존 코드에서 더 채우면 됨) =====

typedef struct Plant {
    RECT body;
    int  kind;
    int  hp;
    int  atk;
    int  tu;      // 살아있음 여부 등
} Plant;

typedef struct Zombie {
    RECT body;
    int  kind;
    int  hp;
    int  tu;
    int  mspeed;
} Zombie;

#define MAX_PLANTS   50
#define MAX_ZOMBIES 100

typedef struct GameState {
    int timeSec;
    int energy;

    Plant  plants[MAX_PLANTS];
    Zombie zombies[MAX_ZOMBIES];

    int gameOver;
} GameState;


// ===== 네트워크 패킷 구조 =====

#pragma pack(push, 1)

typedef struct PACKET_HEADER {
    int Type;  // 1 = 클라 입력, 2 = 서버 상태
    int Size;  // 헤더 제외 데이터 크기
} PACKET_HEADER;

// 클라이언트 → 서버 : 유닛 배치 예시 (나중에 더 늘려도 됨)
typedef struct CL_PLACE_UNIT {
    PACKET_HEADER header;
    int unitKind;
    int row;
    int col;
} CL_PLACE_UNIT;

// 서버 → 클라 : 전체 게임 상태 전송
typedef struct SV_GAME_STATE {
    PACKET_HEADER header;
    GameState state;
} SV_GAME_STATE;

#pragma pack(pop)


// ===== 공용 함수 프로토타입 (game_logic.c에서 구현) =====

#ifdef __cplusplus
extern "C" {
#endif

    void InitGameState(GameState* st);
    void UpdateGameState(GameState* st, float dt);

#ifdef __cplusplus
}
#endif