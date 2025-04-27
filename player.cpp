#include "includes.h"

void Hooks::DoExtraBoneProcessing(int a2, int a3, int a4, int a5, int a6, int a7) {
    // The server doesn't use this so we'll skip it entirely
    // This prevents potential issues with third-party animations
    return;
}

void Hooks::StandardBlendingRules(int a2, int a3, int a4, int a5, int a6) {
    // Cast thisptr to player ptr
    Player* player = (Player*)this;

    // Validate player
    if (!player || (player->index() - 1) > 63 || !player->IsPlayer())
        return g_hooks.m_StandardBlendingRules(this, a2, a3, a4, a5, a6);

    // Store original effects flags
    const int originalEffects = player->m_fEffects();

    // Apply EF_NOINTERP flag to disable interpolation
    // This ensures we have raw, uninterpolated animation data
    player->m_fEffects() |= EF_NOINTERP;

    // Call original function with modified state
    g_hooks.m_StandardBlendingRules(this, a2, a3, a4, a5, a6);

    // Restore original effects flags to avoid unexpected behavior
    player->m_fEffects() = originalEffects;
}

void Hooks::BuildTransformations(int a2, int a3, int a4, int a5, int a6, int a7) {
    // Cast thisptr to player ptr
    Player* player = (Player*)this;

    // Validate player
    if (!player || !player->IsPlayer())
        return g_hooks.m_BuildTransformations(this, a2, a3, a4, a5, a6, a7);

    // Get bone jiggle pointer
    int* bone_jiggle_ptr = reinterpret_cast<int*>(uintptr_t(player) + 0x292C);

    // Store original bone jiggle value
    const int original_bone_jiggle = *bone_jiggle_ptr;

    // Null bone jiggle to prevent attachments from jiggling
    // This makes animations more stable and predictable
    *bone_jiggle_ptr = 0;

    // Call original function with modified state
    g_hooks.m_BuildTransformations(this, a2, a3, a4, a5, a6, a7);

    // Restore original bone jiggle value
    *bone_jiggle_ptr = original_bone_jiggle;
}

void Hooks::CalcView(vec3_t& eye_origin, vec3_t& eye_angles, float& z_near, float& z_far, float& fov) {
    // Cast thisptr to player ptr
    Player* player = (Player*)this;

    // Validate player
    if (!player || !player->IsPlayer())
        return g_hooks.m_1CalcView(this, eye_origin, eye_angles, z_near, z_far, fov);

    // Get pointer to the new animation state flag
    bool* use_new_anim_state_ptr = reinterpret_cast<bool*>((uintptr_t)player + 0x39E1);

    // Store original animation state flag
    const bool original_anim_state = *use_new_anim_state_ptr;

    // Disable new animation state for more consistent animations
    *use_new_anim_state_ptr = false;

    // Call original function with modified state
    g_hooks.m_1CalcView(this, eye_origin, eye_angles, z_near, z_far, fov);

    // Restore original animation state flag
    *use_new_anim_state_ptr = original_anim_state;
}

void Hooks::UpdateClientSideAnimation() {
    // Validate game state
    if (!g_csgo.m_engine->IsInGame() || !g_csgo.m_engine->IsConnected())
        return g_hooks.m_UpdateClientSideAnimation(this);

    // Cast thisptr to player ptr
    Player* player = (Player*)this;

    // Only update animations when explicitly requested
    // This gives us full control over when animations update
    if (!g_cl.m_update_anims)
        return;

    // Track animation update state for debugging
    bool was_updated = false;

    // Create timing scope to measure animation update time
    {
        // Call original function to update animations
        g_hooks.m_UpdateClientSideAnimation(this);
        was_updated = true;
    }

    // Optional: Notify animation system that update occurred
    if (was_updated && player == g_cl.m_local) {
        // Mark that local player animations have been updated this frame
        g_cl.m_animation_updated = true;
    }
}

Weapon* Hooks::GetActiveWeapon() {
    Stack stack;

    // Address used to detect scope effect rendering
    static Address ret_1 = pattern::find(g_csgo.m_client_dll, XOR("85 C0 74 1D 8B 88 ? ? ? ? 85 C9"));

    // Disable scope effect if noscope is enabled
    if (g_menu.main.visuals.noscope.get()) {
        if (stack.ReturnAddress() == ret_1)
            return nullptr;
    }

    // Call original function
    return g_hooks.m_GetActiveWeapon(this);
}

bool Hooks::EntityShouldInterpolate() {
    // Cast thisptr to player ptr
    Player* player = (Player*)this;

    // Validate player
    if (!player)
        return g_hooks.m_EntityShouldInterpolate(this);

    // Always allow interpolation for local player
    // This ensures smooth first-person view and animations
    if (player == g_cl.m_local)
        return true;

    // Disable interpolation for other players to prevent smoothing
    // This gives us more accurate positions for lagcomp/backtracking
    if (player->IsPlayer()) {
        return false;
    }

    // Call original for non-player entities
    bool original_result = g_hooks.m_EntityShouldInterpolate(this);

    // Could add conditions for specific entity types here

    return original_result;
}

void CustomEntityListener::OnEntityCreated(Entity* ent) {
    // Validate entity
    if (!ent)
        return;

    // Handle player creation
    if (ent->IsPlayer()) {
        Player* player = ent->as<Player*>();
        if (!player)
            return;

        // Reset player data in aimbot system
        const int player_index = player->index() - 1;
        if (player_index >= 0 && player_index < 64) {
            AimPlayer* data = &g_aimbot.m_players[player_index];
            if (data)
                data->reset();

            // Setup VMT hooks for the player
            VMT* vmt = &g_hooks.m_player[player_index];
            if (vmt) {
                // Reset and initialize VMT
                vmt->reset();
                vmt->init(player);

                // Apply hooks that should be on all players
                g_hooks.m_DoExtraBoneProcessing = vmt->add<Hooks::DoExtraBoneProcessing_t>(Player::DOEXTRABONEPROCESSING, util::force_cast(&Hooks::DoExtraBoneProcessing));
                g_hooks.m_StandardBlendingRules = vmt->add<Hooks::StandardBlendingRules_t>(Player::STANDARDBLENDINGRULES, util::force_cast(&Hooks::StandardBlendingRules));
                g_hooks.m_BuildTransformations = vmt->add<Hooks::BuildTransformations_t>(Player::BUILDTRANSFORMATIONS, util::force_cast(&Hooks::BuildTransformations));

                // Apply hooks specific to local player
                if (player->index() == g_csgo.m_engine->GetLocalPlayer()) {
                    g_hooks.m_UpdateClientSideAnimation = vmt->add<Hooks::UpdateClientSideAnimation_t>(Player::UPDATECLIENTSIDEANIMATION, util::force_cast(&Hooks::UpdateClientSideAnimation));
                    g_hooks.m_1CalcView = vmt->add<Hooks::CalcView_t>(270, util::force_cast(&Hooks::CalcView));
                    g_hooks.m_GetActiveWeapon = vmt->add<Hooks::GetActiveWeapon_t>(Player::GETACTIVEWEAPON, util::force_cast(&Hooks::GetActiveWeapon));
                }
            }
        }
    }
}

void CustomEntityListener::OnEntityDeleted(Entity* ent) {
    // Validate entity index is in player range
    if (!ent || ent->index() < 1 || ent->index() > 64)
        return;

    // Get player index
    const int player_index = ent->index() - 1;

    // Reset aimbot player data
    AimPlayer* data = &g_aimbot.m_players[player_index];
    if (data)
        data->reset();

    // Reset VMT hooks
    VMT* vmt = &g_hooks.m_player[player_index];
    if (vmt)
        vmt->reset();
}