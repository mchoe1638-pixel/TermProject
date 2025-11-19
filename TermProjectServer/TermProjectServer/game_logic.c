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

    st->zombieCount = 0;

    // plant 전체 초기화
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            Plant* p = &st->plants[r][c];
            p->type = 0;
            p->alive = 0;
            p->row = r;
            p->col = c;
            p->cooldown = 0.0f;
        }
    }
}

// row, col 위치에 plant 한 개 설치
void PlacePlant(GameState* st, int row, int col, int type)
{
    if (row < 0 || row >= MAX_ROWS) return;
    if (col < 0 || col >= MAX_COLS) return;

    Plant* p = &st->plants[row][col];

    // 이미 있는 칸이면 덮어쓸지, 무시할지 옵션
    // 지금은 그냥 덮어쓴다.
    p->type = type;
    p->alive = 1;
    p->row = row;
    p->col = col;
    p->cooldown = 0.0f;  // 공격 쿨다운은 3단계에서 사용
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

static void UpdatePlants(GameState* st, float dt)
{
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            Plant* p = &st->plants[r][c];
            if (!p->alive) continue;

            // 3단계에서 여기서 쿨다운/공격 로직 넣을 예정
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
    UpdatePlants(st, dt);
    UpdateZombies(st, dt);
}