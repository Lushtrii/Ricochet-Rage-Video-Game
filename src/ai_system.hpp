#pragma once

#include <vector>
#include <random>

#include "tiny_ecs_registry.hpp"
#include "common.hpp"
#include "world_init.hpp"

class AISystem
{
	RenderSystem *renderer_arg;
public:
	void init(RenderSystem *renderer_arg);
    void step(float elapsed_ms);

    void spawn_minions(glm::vec2 &position);

    void teleport_boss(Entity &enemy, Motion &playerMotion, EnemyState &enemyState);

private:
    void simple_chase(float elapsed_ms, Motion &playersMotion);
    void simple_chase_enemy(Entity &curr_entity, Motion &playersMotion);
    void stop_and_shoot(Entity &enemy, ReloadTime &counter, float elapsed_ms, Motion &playerMotion, bool boss);
    void single_shot_enemy(Motion &enemyMotion, Motion &playerMotion, ReloadTime &counter);
    void shotgun_enemy(Motion &enemyMotion, Motion &playerMotion, ReloadTime &counter);
    void context_chase(Entity &enemy,  Motion &playerMotion);
    void ranged_enemy_pursue(Entity &enemy, float elapsed_ms, Motion &playerMotion, EnemyState &enemyState);
    void boss_enemy_pursue(Entity &enemy, float elapsed_ms, Motion &playerMotion, EnemyState &enemyState);
    void chase_with_a_star(Pathfinder &pathfinder, float elapsed_ms, Motion &playerMotion, Motion &enemyMotion);
    void update_path(Motion &playerMotion, Motion &enemyMotion, Pathfinder &pathfinder);
    void stop_and_melee(Entity &enemy, MeleeAttack &counter, float elapsed_ms, Motion &playerMotion, Entity &playerEntity);
    bool line_of_sight_check(Entity &enemy, Motion &playerMotion);
    bool line_box_collision(Motion &enemyMotion, Motion &obstacleMotion, vec2 &directionDelta);
    vec2 quadratic_bezier(float t, float max_time);
    void astar_pathfinding(GridMap& grid, GridNode* startNode, GridNode* endNode, Pathfinder &pathfinder);
    void reset_grid(GridMap &gridMap);
    void interpolate_pathfinding(Motion &enemyMotion, Pathfinder &pathfinder, Motion &playerMotion);

    const float rangedEnemySpeed = 125.f;
    const float meleeEnemySpeed = 175.f;
	float const followingConstant = 0.4f;
	float const distanceToWalls = 150.0f;
    float const aggroDistance = 400.0f;
    const float original_ms = 3000;
	const float take_aim_ms = 500;
	const float shoot_rate = 500;
    const float obstacleForce = 25.0f;
    const float enemyForce = 20.0f;
    const float minDistanceToPlayer = 80.0f;
    const float meleeDistance = 100.0f;
    const float distanceBetweenEnemies = 30.0f;
    const float los_increment = 50.0f;
    const float shotgun_angle = M_PI/8.0f;
    const float tp_to_player_range = 300.0f;
    const float minionDistance = 90.0f;

    const int a_star_frame_updates = 100;
    int a_star_frame = 100;

    // C++ random number generator
    std::default_random_engine rng;
    std::uniform_real_distribution<float> uniform_dist; // number between 0..1
};
