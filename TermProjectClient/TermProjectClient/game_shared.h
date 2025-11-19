// game_shared.h
#pragma once

#include <windows.h>

// ===================== 게임 엔티티 =====================

typedef struct Zombie {
    LONG x;   // 화면상의 X좌표
    LONG y;   // 화면상의 Y좌표
    int  hp;  // 나중에 쓸 체력
    int  alive; // 1 = 살아있음, 0 = 죽음
} Zombie;

#define MAX_ZOMBIES 32

typedef struct GameState {
    int timeSec;    // 서버에서 증가시키는 시간(초)
    int energy;     // 나중에 쓸 자원

    Zombie zombies[MAX_ZOMBIES];
    int zombieCount;
} GameState;

// ===================== 패킷 정의 =====================

#pragma pack(push, 1)

typedef struct PACKET_HEADER {
    int Type;   // 2 = 서버 상태 패킷 (나중에 1 = 입력 같은 거 추가)
    int Size;   // 헤더 제외 데이터 크기
} PACKET_HEADER;

typedef struct SV_GAME_STATE {
    PACKET_HEADER header;
    GameState state;
} SV_GAME_STATE;

#pragma pack(pop)

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