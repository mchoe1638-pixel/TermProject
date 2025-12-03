#include "game_shared.h"
#include <string.h>
#include <math.h>
#include <stdlib.h> // rand()

// ===== 난이도 관련 상수 =====
#define BASE_ZOMBIE_SPEED        50.0f   // 웨이브 0 기본 속도
#define BASE_ZOMBIE_SPAWN_INTERVAL 2.0f  // 웨이브 0 기본 스폰 간격(초)
#define BASE_LINE_X  20.0f

// 식물 타입 상수
#define PLANT_TYPE_NONE      0
#define PLANT_TYPE_SHORT_RNG 1  // 단거리 원거리
#define PLANT_TYPE_LONG_RNG  2  // 장거리 원거리
#define PLANT_TYPE_MELEE     3  // 근거리

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
    st->totalWaves = 3;          // 총 3라운드
    st->maxZombiesThisWave = 10; // 1라운드 10마리
    st->spawnedThisWave = 0;
    st->killedThisWave = 0;
    st->gameResult = 0;          // 진행중

    // plant 초기화
    for (int r = 0; r < MAX_ROWS; ++r) {
        for (int c = 0; c < MAX_COLS; ++c) {
            Plant* p = &st->plants[r][c];
            p->type = PLANT_TYPE_NONE;
            p->alive = 0;
            p->row = r;
            p->col = c;
            p->cooldown = 0.0f;
            p->hp = 0;
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

    p->type = type;
    p->alive = 1;
    p->row = row;
    p->col = col;
    p->cooldown = 0.0f;

    // 타입별 체력 설정 (좀비보다 항상 버티기 좋게)
    switch (type) {
    case PLANT_TYPE_SHORT_RNG:
        p->hp = 180;
        break;
    case PLANT_TYPE_LONG_RNG:
        p->hp = 150;
        break;
    case PLANT_TYPE_MELEE:
        p->hp = 220;
        break;
    default:
        p->hp = 0;
        p->alive = 0;
        break;
    }
}

// 발사체 추가 (속도, 데미지 파라미터)
void SpawnProjectile(GameState* st, float x, float y, float speed, int damage)
{
    if (st->projectileCount >= MAX_PROJECTILES)
        ; // 아래에서 빈 슬롯 찾기 때문에 상한만 의미 있음

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
    p->speed = speed;
    p->damage = damage;

    if (idx >= st->projectileCount)
        st->projectileCount = idx + 1;
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

    // 웨이브에 따라 속도 증가
    z->speed = BASE_ZOMBIE_SPEED + st->waveIndex * 20.0f; // 50, 70, 90 ...

    int row = rand() % MAX_ROWS;
    z->y = (LONG)ROW_CENTER_Y(row);
    z->x = GRID_ORIGIN_X + MAX_COLS * CELL_WIDTH + CELL_WIDTH;

    st->spawnedThisWave++;
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

        // 왼쪽으로 이동 (좀비 개별 속도 사용)
        z->x += (LONG)(-z->speed * dt);

        // 베이스 라인 도달?
        if (z->x <= BASE_LINE_X) {
            st->gameOver = 1;
            z->x = BASE_LINE_X;
            continue;
        }

        // plant와 충돌 체크 → 서로 데미지 주기
        for (int r = 0; r < MAX_ROWS; ++r) {
            for (int c = 0; c < MAX_COLS; ++c) {
                Plant* p = &st->plants[r][c];
                if (!p->alive) continue;

                float px = COL_CENTER_X(c);
                float py = ROW_CENTER_Y(r);

                if (fabsf(z->x - px) < 30.0f &&
                    fabsf(z->y - py) < 30.0f) {

                    // 좀비가 식물을 갉아먹음
                    p->hp -= 15;
                    if (p->hp <= 0) {
                        p->alive = 0;
                    }

                    // 식물도 좀비에게 상호 공격 (근접 대미지)
                    z->hp -= 10;
                    if (z->hp <= 0 && z->alive) {
                        z->alive = 0;
                        st->killedThisWave++;
                    }
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
            if (p->type == PLANT_TYPE_NONE) continue;

            // 쿨다운 감소
            p->cooldown -= dt;
            if (p->cooldown < 0.0f) p->cooldown = 0.0f;

            float px = COL_CENTER_X(c);
            float py = ROW_CENTER_Y(r);

            if (p->type == PLANT_TYPE_MELEE) {
                // 근거리 식물: 따로 발사체는 쏘지 않고,
                // 아주 가까이 온 좀비에게 추가 대미지
                for (int i = 0; i < st->zombieCount; ++i) {
                    Zombie* z = &st->zombies[i];
                    if (!z->alive) continue;

                    if (fabsf(z->x - px) < 35.0f &&
                        fabsf(z->y - py) < 35.0f) {

                        // 근거리느라 공격력은 좀 더 쎄게
                        z->hp -= 40;
                        if (z->hp <= 0 && z->alive) {
                            z->alive = 0;
                            st->killedThisWave++;
                        }
                    }
                }
                continue; // 근거리 타입은 발사체 X
            }

            // 여기부터는 원거리 타입 (1, 2)
            float range = 0.0f;
            float projectileSpeed = 0.0f;
            int damage = 0;
            float cooldownReset = 0.0f;

            if (p->type == PLANT_TYPE_SHORT_RNG) {
                range = 200.0f;         // 짧은 사거리
                projectileSpeed = 250.0f;
                damage = 25;
                cooldownReset = 0.7f;
            }
            else if (p->type == PLANT_TYPE_LONG_RNG) {
                range = 600.0f;         // 긴 사거리
                projectileSpeed = 300.0f;
                damage = 15;
                cooldownReset = 0.4f;
            }
            else {
                continue;
            }

            if (p->cooldown > 0.0f)
                continue;

            // 같은 줄(행) + range 안에 있는 좀비가 있는지 확인
            int hasTarget = 0;
            for (int i = 0; i < st->zombieCount; ++i) {
                Zombie* z = &st->zombies[i];
                if (!z->alive) continue;

                // y축 기준으로 같은 줄인지 대충 판정
                if (fabsf(z->y - py) > CELL_HEIGHT * 0.6f)
                    continue;

                float dx = (float)z->x - px;
                if (dx > 0 && dx < range) { // 앞에 있고, 사거리 안이면
                    hasTarget = 1;
                    break;
                }
            }

            if (!hasTarget) continue;

            // 발사체 발사
            SpawnProjectile(st, px, py, projectileSpeed, damage);
            p->cooldown = cooldownReset;
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

        // 화면 밖이면 제거 (가로 800 기준)
        if (p->x > 800.0f) {
            p->alive = 0;
            continue;
        }

        // 좀비와 충돌 체크
        for (int z = 0; z < st->zombieCount; ++z) {
            Zombie* zb = &st->zombies[z];
            if (!zb->alive) continue;

            if (fabsf(p->x - zb->x) < 20.0f &&
                fabsf(p->y - zb->y) < 30.0f) {

                zb->hp -= p->damage;
                p->alive = 0;

                if (zb->hp <= 0 && zb->alive) {
                    zb->alive = 0;
                    st->killedThisWave++;
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

    // 웨이브마다 스폰 간격을 조금씩 줄임
    float spawnInterval = BASE_ZOMBIE_SPAWN_INTERVAL - st->waveIndex * 0.3f;
    if (spawnInterval < 0.8f) spawnInterval = 0.8f; // 너무 짧아지는 것 방지

    // --- 웨이브 스폰 ---
    spawnAcc += dt;
    if (spawnAcc >= spawnInterval) {
        SpawnZombie(st);
        spawnAcc -= spawnInterval;
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

            // 웨이브마다 난이도 올리기: 좀비 수 증가
            st->maxZombiesThisWave += 5;
        }
        else {
            // 마지막 웨이브까지 다 클리어 → 승리
            st->gameResult = 1;
        }
    }
}
// 2025/11/19/최명규/그리드 내 마우스 클릭시 식물 생성, 발사체 발사
// 2025/12/03/최명규/3라운드 난이도, 식물 3종, 상호 공격, 웨이브 난이도 조정
