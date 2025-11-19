// game_logic.c
#include "game_shared.h"
#include <string.h>

void InitGameState(GameState* st)
{
    memset(st, 0, sizeof(GameState));
    st->timeSec = 0;
    st->energy = 0;
}

void UpdateGameState(GameState* st, float dt)
{
    // 일단은 timeSec만 대충 1초 단위로 증가시키자.
    // 나중에 네 좀비/식물 로직 전부 여기 안으로 옮기면 됨.
    static float acc = 0.0f;
    acc += dt;
    if (acc >= 1.0f) {
        st->timeSec += 1;
        acc -= 1.0f;
    }
}