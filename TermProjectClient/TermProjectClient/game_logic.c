// game_logic.c
#include "game_shared.h"
#include <string.h>
#include <math.h> // fabsf 쓰려면

// 좀비 이동 속도 & 스폰 간격 (초 단위)
#define ZOMBIE_SPEED        50.0f   // 초당 50px 왼쪽으로
#define ZOMBIE_SPAWN_INTERVAL 2.0f  // 2초마다 한 마리
#define BASE_LINE_X  20.0f

void InitGameState(GameState* st)
{
    memset(st, 0, sizeof(GameState));

    st->timeSec = 0;
    st->energy = 0;
    st->gameOver = 0;

    st->zombieCount = 0;
    st->projectileCount = 0;

    // 웨이브 기본 값
    st->waveIndex = 0;
    st->totalWaves = 3;          // 일단 3웨이브 정도로
    st->maxZombiesThisWave = 10; // 1웨이브 10마리
    st->spawnedThisWave = 0;
    st->killedThisWave = 0;
    st->gameResult = 0;          // 진행중

    // plant 초기화
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            Plant* p = &st->plants[r][c];
            p->type = 0;
            p->alive = 0;
            p->row = r;
            p->col = c;
            p->cooldown = 0.0f;
            p->hp = 100;   // 기본 체력
        }
    }

    // projectile 초기화
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        st->projectiles[i].alive = 0;
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


static void SpawnZombie(GameState* st)
{
    if (st->zombieCount >= MAX_ZOMBIES)
        return;

    // 이번 웨이브에서 더 이상 스폰할 몹이 없으면 리턴
    if (st->spawnedThisWave >= st->maxZombiesThisWave)
        return;

    Zombie* z = &st->zombies[st->zombieCount++];
    z->hp = 100;
    z->alive = 1;

    int row = rand() % MAX_ROWS;
    z->y = (LONG)ROW_CENTER_Y(row);
    z->x = GRID_ORIGIN_X + MAX_COLS * CELL_WIDTH + CELL_WIDTH;

    st->spawnedThisWave++;
}

// 발사체 추가
void SpawnProjectile(GameState* st, float x, float y)
{
    if (st->projectileCount >= MAX_PROJECTILES)
        return;

    // 빈 슬롯 찾기 (재사용)
    int idx = -1;
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (!st->projectiles[i].alive) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;

    Projectile* p = &st->projectiles[idx];
    p->alive = 1;
    p->x = x;
    p->y = y;
    p->speed = 250.0f;   // 초당 250px 오른쪽
    p->damage = 20;

    // 개수는 대충 "최대 살아있는 수" 정도로 유지
    if (idx >= st->projectileCount)
        st->projectileCount = idx + 1;
}

// 좀비 이동
static void UpdateZombies(GameState* st, float dt)
{
    for (int i = 0; i < st->zombieCount; ++i) {
        Zombie* z = &st->zombies[i];
        if (!z->alive) continue;

        if (st->gameOver) {
            // 게임 끝났으면 더 이상 안 움직이게
            continue;
        }

        // 왼쪽으로 이동
        z->x += (LONG)(-ZOMBIE_SPEED * dt);

        // 베이스 라인 도달?
        if (z->x <= BASE_LINE_X) {
            st->gameOver = 1;
            z->x = BASE_LINE_X;
            continue;
        }

        // plant와 충돌 체크 (대충 같은 그리드 열과 y 근처면 plant 먹기)
        for (int r = 0; r < MAX_ROWS; ++r) {
            for (int c = 0; c < MAX_COLS; ++c) {
                Plant* p = &st->plants[r][c];
                if (!p->alive) continue;

                // 그리드 중앙 좌표 (Render에서 쓴 것과 맞추기)
                float px = COL_CENTER_X(c);
                float py = ROW_CENTER_Y(r);

                if (fabsf(z->x - px) < 30.0f &&
                    fabsf(z->y - py) < 30.0f) {
                    // plant를 씹어먹는다고 가정: 체력 깎기
                    p->hp -= 50;
                    if (p->hp <= 0) {
                        p->alive = 0;
                    }
                    // 좀비는 계속 진행 (원하면 여기서 멈춰있게 바꿀 수도 있음)
                }
            }
        }
    }
}

static void UpdatePlants(GameState* st, float dt)
{
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            Plant* p = &st->plants[r][c];
            if (!p->alive) continue;
            if (p->type != 1) continue; // 일단 type=1만 공격 plant

            // 쿨다운 감소
            p->cooldown -= dt;
            if (p->cooldown > 0.0f)
                continue;

            // 같은 row에 살아있는 좀비가 하나라도 있으면 쏜다
            int hasTarget = 0;
            for (int i = 0; i < st->zombieCount; ++i) {
                Zombie* z = &st->zombies[i];
                if (!z->alive) continue;
                // 행(row) 대충 y 기준으로 판단해도 되지만, 일단 라인 개념 없으니 전체로 쏘자
                // 나중에 row -> y 매핑해서 같은 줄만 쏘게 바꿀 수 있음
                hasTarget = 1;
                break;
            }

            if (!hasTarget) {
                // 타겟 없으면 굳이 안 쏨 (원하면 쏘게 바꿔도 됨)
                continue;
            }

            // 이 plant의 중앙 위치에서 발사
            float px = COL_CENTER_X(c);
            float py = ROW_CENTER_Y(r);
            SpawnProjectile(st, px, py);

            // 발사 후 쿨다운 재설정 (0.7초마다 한 발)
            p->cooldown = 0.7f;
        }
    }
}

// 발사체 이동, 좀비 충돌
static void UpdateProjectiles(GameState* st, float dt)
{
    for (int i = 0; i < st->projectileCount; ++i) {
        Projectile* p = &st->projectiles[i];
        if (!p->alive) continue;

        // 이동 (오른쪽으로)
        p->x += p->speed * dt;

        // 화면 밖이면 제거 (640은 대충 화면 끝)
        if (p->x > 800.0f) {
            p->alive = 0;
            continue;
        }

        // 좀비와 충돌 체크
        for (int z = 0; z < st->zombieCount; ++z) {
            Zombie* zb = &st->zombies[z];
            if (!zb->alive) continue;

            // 단순 박스 충돌 (반지름 20)
            if (fabsf(p->x - zb->x) < 20.0f &&
                fabsf(p->y - zb->y) < 30.0f) {
                // 히트
                zb->hp -= p->damage;
                p->alive = 0;

                if (zb->hp <= 0) {
                    if (zb->alive) {
                        st->killedThisWave++;
                    }
                    zb->alive = 0;
                }
                break;
            }
        }
    }
}

void UpdateGameState(GameState* st, float dt)
{
    // 패배 상태면 더 이상 진행 X
    if (st->gameOver || st->gameResult == 2) {
        st->gameResult = 2;
        return;
    }

    // 이미 승리했으면 더 이상 진행 X
    if (st->gameResult == 1) {
        return;
    }

    static float accSec = 0.0f;
    static float spawnAcc = 0.0f;

    accSec += dt;
    if (accSec >= 1.0f) {
        st->timeSec += 1;
        accSec -= 1.0f;
    }

    // --- 웨이브 스폰 ---
    spawnAcc += dt;
    if (spawnAcc >= ZOMBIE_SPAWN_INTERVAL) {
        SpawnZombie(st);
        spawnAcc -= ZOMBIE_SPAWN_INTERVAL;
    }

    // 엔티티 업데이트
    UpdatePlants(st, dt);
    UpdateProjectiles(st, dt);
    UpdateZombies(st, dt);

    // 베이스 도달로 gameOver가 되면 게임 결과 = 패배
    if (st->gameOver) {
        st->gameResult = 2; // 패배
        return;
    }

    // 현재 살아있는 좀비 수 계산
    int aliveZombies = 0;
    for (int i = 0; i < st->zombieCount; ++i) {
        if (st->zombies[i].alive) aliveZombies++;
    }

    // 이번 웨이브의 몹을 모두 스폰했고, 모두 죽었으면
    if (st->spawnedThisWave >= st->maxZombiesThisWave && aliveZombies == 0) {
        if (st->waveIndex + 1 < st->totalWaves) {
            // 다음 웨이브로 넘어감
            st->waveIndex++;
            st->spawnedThisWave = 0;
            st->killedThisWave = 0;

            // 웨이브마다 난이도 살짝 올리기 (예: 좀비 수 증가)
            st->maxZombiesThisWave += 5;
        }
        else {
            // 마지막 웨이브까지 다 클리어 → 승리
            st->gameResult = 1;
        }
    }
}

// 2025/11/19/최명규/그리드 내 마우스 클릭시 식물 생성, 발사체 발사