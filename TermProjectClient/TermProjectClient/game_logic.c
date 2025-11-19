// game_logic.c
#include "game_shared.h"
#include <string.h>

void InitGameState(GameState* st)
{
    memset(st, 0, sizeof(GameState));

    st->timeSec = 0;
    st->energy = 0;

    // 테스트용으로 좀비 한 마리 생성
    st->zombieCount = 1;
    st->zombies[0].x = 600;   // 화면 오른쪽 근처
    st->zombies[0].y = 200;   // 대충 중간 높이
    st->zombies[0].hp = 100;
    st->zombies[0].alive = 1;
}

// 좀비 업데이트 (왼쪽으로 이동)
static void UpdateZombies(GameState* st, float dt)
{
    const float speed = 100.0f; // 초당 100픽셀 정도

    for (int i = 0; i < st->zombieCount; ++i) {
        Zombie* z = &st->zombies[i];
        if (!z->alive) continue;

        // x좌표를 왼쪽으로 이동
        float dx = -speed * dt;
        z->x += (LONG)dx;

        // 화면 왼쪽 끝 도달 시 멈추게 하자
        if (z->x < 0) {
            z->x = 0;
        }
    }
}

void UpdateGameState(GameState* st, float dt)
{
    // 시간 누적 (대충 1초 단위로만 올리자)
    static float accSec = 0.0f;
    accSec += dt;
    if (accSec >= 1.0f) {
        st->timeSec += 1;
        accSec -= 1.0f;
    }

    // 좀비 이동
    UpdateZombies(st, dt);
}
// 2025/11/11/최명규/LogicTick에서 좀비 움직임-------------------------------------