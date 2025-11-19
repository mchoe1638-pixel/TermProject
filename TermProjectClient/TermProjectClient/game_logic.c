// game_logic.c
#include "game_shared.h"
#include <string.h>

// 좀비 이동 속도 & 스폰 간격 (초 단위)
#define ZOMBIE_SPEED        50.0f   // 초당 50px 왼쪽으로
#define ZOMBIE_SPAWN_INTERVAL 2.0f  // 2초마다 한 마리

void InitGameState(GameState* st)
{
    memset(st, 0, sizeof(GameState));

    st->timeSec = 0;
    st->energy = 0;
    st->zombieCount = 0;   // 시작할 때는 좀비 없음
}

// 한 마리 추가
static void SpawnZombie(GameState* st)
{
    if (st->zombieCount >= MAX_ZOMBIES)
        return;

    Zombie* z = &st->zombies[st->zombieCount++];

    // 오른쪽에서 왼쪽으로 걸어오게
    z->x = 600;      // 창 가로 640 기준이면 거의 오른쪽
    z->y = 200;      // 대충 중간 라인 하나 (나중에 row로 바꾸면 됨)
    z->hp = 100;
    z->alive = 1;
}

// 좀비 이동
static void UpdateZombies(GameState* st, float dt)
{
    for (int i = 0; i < st->zombieCount; ++i) {
        Zombie* z = &st->zombies[i];
        if (!z->alive) continue;

        z->x += (LONG)(-ZOMBIE_SPEED * dt);

        if (z->x < 0) {
            z->x = 0;
            // 나중에 여기서 base 도달 → 게임오버 처리 가능
        }
    }
}

void UpdateGameState(GameState* st, float dt)
{
    // 1) 서버 시간 증가 (초 단위)
    static float accSec = 0.0f;
    accSec += dt;
    if (accSec >= 1.0f) {
        st->timeSec += 1;
        accSec -= 1.0f;
    }

    // 2) 일정 간격으로 좀비 자동 스폰
    static float spawnAcc = 0.0f;
    spawnAcc += dt;
    if (spawnAcc >= ZOMBIE_SPAWN_INTERVAL) {
        SpawnZombie(st);
        spawnAcc -= ZOMBIE_SPAWN_INTERVAL;
    }

    // 3) 모든 좀비 이동
    UpdateZombies(st, dt);
}