#include "item_display.h"

#include "render.h"
#include "../../common.h"  /* For copyspritex and PL_* flags */
#include "sprite_data.h"
#include "sdl.h"
#include "../ui/imgui/imgui_wrapper.h"
#include "config/config.h"
#include "shaders/effect_shader.h"

void render_shop_item_display(const ItemDisplayInfo *item, int x, int y, int effect) {
    if (!item || !item->sprite) return;

    /* Draw base sprite */
    copyspritex(item->sprite, x, y, effect);

    /* Draw overlays in order */
    if (item->properties.shop_info.soulstone)
        copyspritex(SPRITE_OVERLAY_SOULSTONE, x, y, effect);

    if (item->properties.shop_info.talisman)
        copyspritex(SPRITE_OVERLAY_TALISMAN, x, y, effect);

    /* Draw catalyst overlay (properties = catalyst ID) */
    if (item->flags > 0)
        copyspritex(SPRITE_OVERLAY_CATALYST_BASE + (int) item->flags, x, y, effect);
}

void render_lockable_item_display(const ItemDisplayInfo *item, int x, int y, int effect) {
    if (!item || !item->sprite) return;

    /* Draw base sprite */
    copyspritex(item->sprite, x, y, effect);

    /* Draw overlays in order */
    if (item->flags & LOCKABLE_ITEM_FLAG_SOULSTONE)
        copyspritex(SPRITE_OVERLAY_SOULSTONE, x, y, effect);

    if (item->flags & LOCKABLE_ITEM_FLAG_TALISMAN)
        copyspritex(SPRITE_OVERLAY_TALISMAN, x, y, effect);

    if (item->flags & LOCKABLE_ITEM_FLAG_CORRUPTION)
        copyspritex(SPRITE_OVERLAY_CORRUPTION, x, y, effect);

    /* Draw catalyst overlay (properties = catalyst ID) */
    if (item->properties.catalyst_id > 0)
        copyspritex(SPRITE_OVERLAY_CATALYST_BASE + (int) item->properties.catalyst_id, x, y, effect);

    /* Draw stack count overlay */
    if (item->stack > 0 && item->stack <= 10)
        copyspritex(SPRITE_OVERLAY_STACK_BASE + item->stack, x, y, effect);
}

void render_worn_item_display(const ItemDisplayInfo *item, int x, int y, int effect) {
    if (!item || !item->sprite) return;

    /* Draw base sprite */
    copyspritex(item->sprite, x, y, effect);

    /* Draw overlays in order */
    if (item->properties.worn_flags & PL_SOULSTONED)
        copyspritex(SPRITE_OVERLAY_SOULSTONE, x, y, effect);
    if (item->properties.worn_flags & PL_ENCHANTED)
        copyspritex(SPRITE_OVERLAY_TALISMAN, x, y, effect);
    if (item->properties.worn_flags & PL_CORRUPTED)
        copyspritex(SPRITE_OVERLAY_CORRUPTION, x, y, effect);

    if (item->stack > 0 && item->stack <= 10)
        copyspritex(SPRITE_OVERLAY_STACK_BASE + item->stack, x, y, effect);
}

void render_item_display(const ItemDisplayInfo *item, int x, int y, int effect) {
    if (!item || !item->sprite) return;

    /* Draw base sprite */
    copyspritex(item->sprite, x, y, effect);

    /* Draw overlays in order */
    if (item->flags & ITEM_FLAG_SOULSTONE)
        copyspritex(SPRITE_OVERLAY_SOULSTONE, x, y, effect);

    if (item->flags & ITEM_FLAG_TALISMAN)
        copyspritex(SPRITE_OVERLAY_TALISMAN, x, y, effect);

    if (item->flags & ITEM_FLAG_CORRUPTION)
        copyspritex(SPRITE_OVERLAY_CORRUPTION, x, y, effect);

    /* Draw catalyst overlay (properties = catalyst ID) */
    if (item->properties.catalyst_id > 0)
        copyspritex(SPRITE_OVERLAY_CATALYST_BASE + (int) item->properties.catalyst_id, x, y, effect);

    /* Draw stack count overlay */
    if (item->stack > 0 && item->stack <= 10)
        copyspritex(SPRITE_OVERLAY_STACK_BASE + item->stack, x, y, effect);
}

/* ===== ImGui Draw List Versions ===== */

/* Data for effect shader callback */
typedef struct {
    int effect;
} EffectShaderData;

/* Callback to enable effect shader */
static void enable_effect_shader_callback(void *user_data) {
    int effect = (int) (intptr_t) user_data;

    float gamma_scale = g_config.video.gamma / 5000.0f;
    float shade_effect, gamma_effect;
    bool grey, infra, water, red, green, invis, buff;
    calculate_gamma_shader_params(effect, &shade_effect, &gamma_effect, &grey, &infra, &water, &red, &green, &invis,
                                  &buff);

    const EffectShaderSettings settings = {
        .red = red,
        .gamma_scale = gamma_scale,
        .shade_effect = shade_effect,
        .gamma_effect = gamma_effect,
    };

    /* Use ImGui-compatible effect shader (vec2 position, not vec3) */
    use_effect_shader_imgui(settings);
}

/* Helper to draw a single sprite with ImGui draw_list */
static void draw_sprite_imgui(void *draw_list, int sprite_id, float x, float y, int effect) {
    if (sprite_id <= 0 || !sprite_data) return;

    sdl_load_sprite(sprite_id);
    SpriteData *sprite = &sprite_data[sprite_id];
    if (!sprite->loaded_in_atlas) return;

    /* If effect is needed, switch shaders */
    imgui_draw_list_add_callback(draw_list, enable_effect_shader_callback, (void *) (intptr_t) effect);

    /* Draw sprite using ImGui */
    void *texture = (void *) (uintptr_t) sprite->atlas_texture;
    imgui_draw_list_add_image(draw_list, texture,
                              x, y,
                              x + sprite->pixel_width, y + sprite->pixel_height,
                              sprite->uv0.u, sprite->uv0.v,
                              sprite->uv1.u, sprite->uv1.v,
                              0xFFFFFFFF);


    imgui_draw_list_reset_render_state(draw_list);
}

void render_lockable_item_display_imgui(void *draw_list, const ItemDisplayInfo *item,
                                        float x, float y, int effect) {
    if (!item || !item->sprite || !draw_list) return;

    /* Draw base sprite with effect */
    draw_sprite_imgui(draw_list, item->sprite, x, y, effect);

    /* Draw overlays without effect */
    if (item->flags & LOCKABLE_ITEM_FLAG_SOULSTONE)
        draw_sprite_imgui(draw_list, SPRITE_OVERLAY_SOULSTONE, x, y, effect);

    if (item->flags & LOCKABLE_ITEM_FLAG_TALISMAN)
        draw_sprite_imgui(draw_list, SPRITE_OVERLAY_TALISMAN, x, y, effect);

    if (item->flags & LOCKABLE_ITEM_FLAG_CORRUPTION)
        draw_sprite_imgui(draw_list, SPRITE_OVERLAY_CORRUPTION, x, y, effect);

    if (item->properties.catalyst_id > 0)
        draw_sprite_imgui(draw_list, SPRITE_OVERLAY_CATALYST_BASE + (int) item->properties.catalyst_id, x, y, effect);

    if (item->stack > 0 && item->stack <= 10)
        draw_sprite_imgui(draw_list, SPRITE_OVERLAY_STACK_BASE + item->stack, x, y, effect);
}
