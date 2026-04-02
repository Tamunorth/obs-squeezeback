--[[
  Squeezeback - Scene Zoom Animation for OBS Studio

  Studio Mode workflow:
  1. Design your scene: arrange video source + L-shape graphic as the final layout
  2. In script settings, pick the scene and the video source, click "Capture Layout"
  3. When the scene enters PREVIEW, it's instantly zoomed to show only the video fullscreen
  4. When you transition to PROGRAM, OBS duplicates the zoomed preview
  5. After the transition, the script animates the zoom-out on the PROGRAM duplicate
  6. The L-shape slides in from the edges on the live output
]]

obs = obslua

-- ── Settings ──
local source_name = ""
local scene_name = ""
local anim_duration = 0.8
local anim_delay = 0.1
local easing_type = 1
local auto_trigger = true

-- ── Runtime state ──
local is_animating = false
local anim_elapsed = 0.0
local anim_started = false
local reverse_animating = false
local is_squeezed = false
local layout_captured = false
local event_guard = false  -- prevents re-entrant event handling

-- Timer
local TIMER_INTERVAL_MS = 16
local SECS_PER_FRAME = TIMER_INTERVAL_MS / 1000.0

-- Zoom parameters
local zoom_scale = 1.0
local zoom_offset_x = 0
local zoom_offset_y = 0
local canvas_width = 1920
local canvas_height = 1080

-- Stored designed transforms for ALL scene items
local stored_items = {}

-- The scene source we're currently animating on (the program duplicate)
-- Must be released when animation completes
local anim_scene_ref = nil

-- Hotkey
local hotkey_trigger_id = nil

-- ── Easing functions ──

local function ease_linear(t) return t end

local function ease_out_cubic(t)
    local u = 1.0 - t
    return 1.0 - u * u * u
end

local function ease_in_out_cubic(t)
    if t < 0.5 then
        return 4.0 * t * t * t
    else
        local u = -2.0 * t + 2.0
        return 1.0 - (u * u * u) / 2.0
    end
end

local function ease_out_expo(t)
    if t >= 1.0 then return 1.0 end
    return 1.0 - math.pow(2.0, -10.0 * t)
end

local function ease_out_back(t)
    local c1 = 1.70158
    local c3 = c1 + 1.0
    local u = t - 1.0
    return 1.0 + c3 * u * u * u + c1 * u * u
end

local function apply_easing(t)
    t = math.max(0.0, math.min(1.0, t))
    if easing_type == 0 then return ease_linear(t) end
    if easing_type == 1 then return ease_out_cubic(t) end
    if easing_type == 2 then return ease_in_out_cubic(t) end
    if easing_type == 3 then return ease_out_expo(t) end
    if easing_type == 4 then return ease_out_back(t) end
    return ease_out_cubic(t)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

-- ── Release the animation scene reference ──
local function release_anim_ref()
    if anim_scene_ref then
        obs.obs_source_release(anim_scene_ref)
        anim_scene_ref = nil
    end
end

-- ── Get scene by name (for preview/capture) ──
local function get_scene_by_name()
    local src = obs.obs_get_source_by_name(scene_name)
    if not src then return nil, nil end
    local scene = obs.obs_scene_from_source(src)
    return scene, src
end

-- ── Capture designed layout ──
local function capture_layout()
    local scene, scene_src = get_scene_by_name()
    if not scene then
        obs.script_log(obs.LOG_WARNING, "[Squeezeback] Scene '" .. scene_name .. "' not found")
        return false
    end

    local video_info = obs.obs_video_info()
    if obs.obs_get_video_info(video_info) then
        canvas_width = video_info.base_width
        canvas_height = video_info.base_height
    end

    stored_items = {}
    local video_screen_rect = nil

    local items = obs.obs_scene_enum_items(scene)
    if items then
        for _, item in ipairs(items) do
            local item_source = obs.obs_sceneitem_get_source(item)
            local name = obs.obs_source_get_name(item_source)

            local info = obs.obs_transform_info()
            obs.obs_sceneitem_get_info2(item, info)

            local crop = obs.obs_sceneitem_crop()
            obs.obs_sceneitem_get_crop(item, crop)

            local entry = {
                name = name,
                pos_x = info.pos.x, pos_y = info.pos.y,
                rot = info.rot,
                scale_x = info.scale.x, scale_y = info.scale.y,
                alignment = info.alignment,
                bounds_type = info.bounds_type,
                bounds_alignment = info.bounds_alignment,
                bounds_x = info.bounds.x, bounds_y = info.bounds.y,
                crop_to_bounds = info.crop_to_bounds,
                crop_left = crop.left, crop_top = crop.top,
                crop_right = crop.right, crop_bottom = crop.bottom,
            }

            local box_transform = obs.matrix4()
            obs.obs_sceneitem_get_box_transform(item, box_transform)
            entry.screen_x = box_transform.t.x
            entry.screen_y = box_transform.t.y
            entry.screen_w = math.sqrt(box_transform.x.x^2 + box_transform.x.y^2)
            entry.screen_h = math.sqrt(box_transform.y.x^2 + box_transform.y.y^2)

            table.insert(stored_items, entry)

            obs.script_log(obs.LOG_INFO, string.format(
                "[Squeezeback]   '%s': screen(%.0f,%.0f %.0fx%.0f)",
                name, entry.screen_x, entry.screen_y, entry.screen_w, entry.screen_h))

            if name == source_name then
                video_screen_rect = { x = entry.screen_x, y = entry.screen_y,
                                      w = entry.screen_w, h = entry.screen_h }
            end
        end
        obs.sceneitem_list_release(items)
    end

    obs.obs_source_release(scene_src)

    if not video_screen_rect or video_screen_rect.w < 1 or video_screen_rect.h < 1 then
        obs.script_log(obs.LOG_WARNING, "[Squeezeback] Video source not found or has zero size")
        return false
    end

    zoom_scale = math.min(canvas_width / video_screen_rect.w,
                          canvas_height / video_screen_rect.h)
    zoom_offset_x = video_screen_rect.x
    zoom_offset_y = video_screen_rect.y

    layout_captured = true
    obs.script_log(obs.LOG_INFO, string.format(
        "[Squeezeback] Layout captured: %d items, zoom=%.3f", #stored_items, zoom_scale))
    return true
end

-- ── Apply zoom to a SPECIFIC scene source's items ──
-- scene_source: the obs_source_t to modify (can be preview or program duplicate)
-- t: 0.0 = zoomed in (video fullscreen), 1.0 = designed layout
local function apply_zoom_on(scene_source, t)
    if not scene_source then return end
    local scene = obs.obs_scene_from_source(scene_source)
    if not scene then return end

    local items = obs.obs_scene_enum_items(scene)
    if not items then return end

    local ALIGN_TOP_LEFT = 5

    for _, item in ipairs(items) do
        local item_source = obs.obs_sceneitem_get_source(item)
        local name = obs.obs_source_get_name(item_source)

        local stored = nil
        for _, s in ipairs(stored_items) do
            if s.name == name then stored = s; break end
        end

        if stored and stored.screen_w > 0 and stored.screen_h > 0 then
            local zoomed_x = (stored.screen_x - zoom_offset_x) * zoom_scale
            local zoomed_y = (stored.screen_y - zoom_offset_y) * zoom_scale
            local zoomed_w = stored.screen_w * zoom_scale
            local zoomed_h = stored.screen_h * zoom_scale

            local cur_x = lerp(zoomed_x, stored.screen_x, t)
            local cur_y = lerp(zoomed_y, stored.screen_y, t)
            local cur_w = lerp(zoomed_w, stored.screen_w, t)
            local cur_h = lerp(zoomed_h, stored.screen_h, t)

            obs.obs_sceneitem_defer_update_begin(item)

            obs.obs_sceneitem_set_alignment(item, ALIGN_TOP_LEFT)
            obs.obs_sceneitem_set_bounds_type(item, obs.OBS_BOUNDS_SCALE_INNER)
            obs.obs_sceneitem_set_bounds_alignment(item, ALIGN_TOP_LEFT)

            local pos = obs.vec2()
            pos.x = cur_x; pos.y = cur_y
            obs.obs_sceneitem_set_pos(item, pos)

            local bounds = obs.vec2()
            bounds.x = cur_w; bounds.y = cur_h
            obs.obs_sceneitem_set_bounds(item, bounds)

            local scale = obs.vec2()
            scale.x = 1.0; scale.y = 1.0
            obs.obs_sceneitem_set_scale(item, scale)

            obs.obs_sceneitem_defer_update_end(item)
        end
    end

    obs.sceneitem_list_release(items)
end

-- ── Restore designed layout on a SPECIFIC scene source ──
local function restore_layout_on(scene_source)
    if not scene_source then return end
    local scene = obs.obs_scene_from_source(scene_source)
    if not scene then return end

    local items = obs.obs_scene_enum_items(scene)
    if not items then return end

    for _, item in ipairs(items) do
        local item_source = obs.obs_sceneitem_get_source(item)
        local name = obs.obs_source_get_name(item_source)

        for _, stored in ipairs(stored_items) do
            if stored.name == name then
                obs.obs_sceneitem_defer_update_begin(item)

                local info = obs.obs_transform_info()
                info.pos.x = stored.pos_x; info.pos.y = stored.pos_y
                info.rot = stored.rot
                info.scale.x = stored.scale_x; info.scale.y = stored.scale_y
                info.alignment = stored.alignment
                info.bounds_type = stored.bounds_type
                info.bounds_alignment = stored.bounds_alignment
                info.bounds.x = stored.bounds_x; info.bounds.y = stored.bounds_y
                info.crop_to_bounds = stored.crop_to_bounds
                obs.obs_sceneitem_set_info2(item, info)

                local crop = obs.obs_sceneitem_crop()
                crop.left = stored.crop_left; crop.top = stored.crop_top
                crop.right = stored.crop_right; crop.bottom = stored.crop_bottom
                obs.obs_sceneitem_set_crop(item, crop)

                obs.obs_sceneitem_defer_update_end(item)
                break
            end
        end
    end

    obs.sceneitem_list_release(items)
end

-- ── Legacy wrappers (for test buttons that operate on the named scene) ──
local function apply_zoom_at(t)
    local scene, src = get_scene_by_name()
    if scene then
        apply_zoom_on(src, t)
        obs.obs_source_release(src)
    end
end

local function restore_designed_layout()
    local scene, src = get_scene_by_name()
    if scene then
        restore_layout_on(src)
        obs.obs_source_release(src)
    end
end

-- ── Timer callback ──
local function on_timer()
    if not is_animating then return end

    anim_elapsed = anim_elapsed + SECS_PER_FRAME

    if not anim_started then
        if anim_elapsed >= anim_delay then
            anim_started = true
            anim_elapsed = 0.0
        end
        return
    end

    local t = anim_elapsed / anim_duration
    if t >= 1.0 then
        is_animating = false

        if reverse_animating then
            if anim_scene_ref then
                apply_zoom_on(anim_scene_ref, 0.0)
            else
                apply_zoom_at(0.0)
            end
            is_squeezed = false
            reverse_animating = false
            obs.script_log(obs.LOG_INFO, "[Squeezeback] Reverse complete")
        else
            if anim_scene_ref then
                restore_layout_on(anim_scene_ref)
            else
                restore_designed_layout()
            end
            is_squeezed = true
            obs.script_log(obs.LOG_INFO, "[Squeezeback] Squeeze complete")
        end

        release_anim_ref()
        return
    end

    local eased = apply_easing(t)
    local target_scene = anim_scene_ref

    if reverse_animating then
        if target_scene then
            apply_zoom_on(target_scene, 1.0 - eased)
        else
            apply_zoom_at(1.0 - eased)
        end
    else
        if target_scene then
            apply_zoom_on(target_scene, eased)
        else
            apply_zoom_at(eased)
        end
    end
end

-- ── Trigger squeeze on a specific scene source ──
local function trigger_squeezeback_on(scene_source)
    if is_animating then return end
    if not layout_captured then
        obs.script_log(obs.LOG_WARNING, "[Squeezeback] Layout not captured!")
        return
    end

    release_anim_ref()

    -- Keep a reference to the scene we're animating
    if scene_source then
        obs.obs_source_addref(scene_source)
        anim_scene_ref = scene_source
    end

    -- Set zoomed-in state on the target scene
    if scene_source then
        apply_zoom_on(scene_source, 0.0)
    else
        apply_zoom_at(0.0)
    end

    is_animating = true
    anim_started = false
    anim_elapsed = 0.0
    reverse_animating = false
    is_squeezed = false

    obs.script_log(obs.LOG_INFO, "[Squeezeback] Squeeze IN started")
end

-- ── Trigger squeeze (test button, uses named scene) ──
local function trigger_squeezeback()
    if source_name == "" or scene_name == "" then return end
    trigger_squeezeback_on(nil)
end

-- ── Trigger reverse ──
local function trigger_reverse()
    if is_animating then return end
    if not layout_captured then return end

    release_anim_ref()
    is_animating = true
    anim_started = true
    anim_elapsed = 0.0
    reverse_animating = true
    obs.script_log(obs.LOG_INFO, "[Squeezeback] Squeeze OUT started")
end

-- ── Toggle ──
local function trigger_toggle(pressed)
    if not pressed then return end
    if is_squeezed then
        trigger_reverse()
    else
        trigger_squeezeback()
    end
end

-- ── Scene change handler (two-phase: preview zoom + program animate) ──
local function on_frontend_event(event)
    if not layout_captured or source_name == "" or scene_name == "" then return end
    if event_guard then return end  -- prevent re-entrant calls

    -- PHASE A: Scene entered PREVIEW -> instantly zoom to fullscreen video
    if event == obs.OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED then
        local preview = obs.obs_frontend_get_current_preview_scene()
        if preview then
            local preview_name = obs.obs_source_get_name(preview)

            if preview_name == scene_name then
                obs.script_log(obs.LOG_INFO, "[Squeezeback] Pre-zooming preview")
                event_guard = true
                apply_zoom_on(preview, 0.0)
                event_guard = false
            end
            obs.obs_source_release(preview)
        end
    end

    -- PHASE B: Scene went to PROGRAM -> animate zoom-out on program duplicate
    if event == obs.OBS_FRONTEND_EVENT_SCENE_CHANGED then
        -- Skip if already animating on this scene
        if is_animating then return end

        local program = obs.obs_frontend_get_current_scene()
        if program then
            local program_name = obs.obs_source_get_name(program)

            if program_name == scene_name then
                obs.script_log(obs.LOG_INFO, "[Squeezeback] Program matched, starting animation")

                event_guard = true

                release_anim_ref()
                anim_scene_ref = program  -- take ownership (already addref'd)

                -- Ensure zoomed state on the program duplicate
                apply_zoom_on(anim_scene_ref, 0.0)

                is_animating = true
                anim_started = false
                anim_elapsed = 0.0
                reverse_animating = false
                is_squeezed = false

                event_guard = false

                obs.script_log(obs.LOG_INFO, "[Squeezeback] Squeeze IN started on program")
                return  -- don't release program, anim_scene_ref owns it
            end
            obs.obs_source_release(program)
        end
    end
end

-- ── OBS Script Interface ──

function script_description()
    return [[<h2>Squeezeback</h2>
<p>Broadcast-style squeezeback animation for Studio Mode.</p>
<h3>How it works:</h3>
<ol>
<li>Arrange your video + L-shape graphic in a scene (the final layout)</li>
<li>Select the scene and video source below</li>
<li>Click <b>"Capture Layout"</b></li>
<li>Enable "Auto-trigger" below</li>
</ol>
<p>When the scene enters preview, it shows the video fullscreen.
When you transition to program, the video smoothly zooms out to reveal the L-shape on the live output.</p>
<p><b>Click "Capture Layout" whenever you rearrange sources.</b></p>]]
end

function script_properties()
    local props = obs.obs_properties_create()

    local scene_list = obs.obs_properties_add_list(props, "scene_name",
        "Scene", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    local scenes = obs.obs_frontend_get_scenes()
    if scenes then
        for _, s in ipairs(scenes) do
            obs.obs_property_list_add_string(scene_list,
                obs.obs_source_get_name(s), obs.obs_source_get_name(s))
        end
        obs.source_list_release(scenes)
    end

    local source_list = obs.obs_properties_add_list(props, "source_name",
        "Video Source (zoom target)", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    local sources = obs.obs_enum_sources()
    if sources then
        for _, src in ipairs(sources) do
            local flags = obs.obs_source_get_output_flags(src)
            if bit.band(flags, obs.OBS_SOURCE_VIDEO) ~= 0 then
                local name = obs.obs_source_get_name(src)
                obs.obs_property_list_add_string(source_list, name, name)
            end
        end
        obs.source_list_release(sources)
    end

    obs.obs_properties_add_button(props, "btn_capture", "Capture Layout",
        function() capture_layout(); return true end)

    obs.obs_properties_add_float_slider(props, "anim_duration",
        "Animation Duration (seconds)", 0.2, 3.0, 0.1)
    obs.obs_properties_add_float_slider(props, "anim_delay",
        "Delay Before Animation (seconds)", 0.0, 2.0, 0.1)

    local easing_list = obs.obs_properties_add_list(props, "easing_type",
        "Easing", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_INT)
    obs.obs_property_list_add_int(easing_list, "Linear", 0)
    obs.obs_property_list_add_int(easing_list, "Ease Out Cubic (Smooth)", 1)
    obs.obs_property_list_add_int(easing_list, "Ease In-Out Cubic", 2)
    obs.obs_property_list_add_int(easing_list, "Ease Out Expo (Fast)", 3)
    obs.obs_property_list_add_int(easing_list, "Ease Out Back (Overshoot)", 4)

    obs.obs_properties_add_bool(props, "auto_trigger",
        "Auto-trigger (zoom preview, animate on program cut)")

    obs.obs_properties_add_button(props, "btn_trigger", "Test Squeeze",
        function() trigger_squeezeback(); return true end)
    obs.obs_properties_add_button(props, "btn_reverse", "Test Reverse",
        function() trigger_reverse(); return true end)
    obs.obs_properties_add_button(props, "btn_restore", "Restore Layout",
        function() if layout_captured then restore_designed_layout(); is_squeezed = true end; return true end)

    return props
end

function script_defaults(settings)
    obs.obs_data_set_default_double(settings, "anim_duration", 0.8)
    obs.obs_data_set_default_double(settings, "anim_delay", 0.1)
    obs.obs_data_set_default_int(settings, "easing_type", 1)
    obs.obs_data_set_default_bool(settings, "auto_trigger", true)
end

function script_update(settings)
    local new_source = obs.obs_data_get_string(settings, "source_name")
    local new_scene = obs.obs_data_get_string(settings, "scene_name")
    anim_duration = obs.obs_data_get_double(settings, "anim_duration")
    anim_delay = obs.obs_data_get_double(settings, "anim_delay")
    easing_type = obs.obs_data_get_int(settings, "easing_type")
    auto_trigger = obs.obs_data_get_bool(settings, "auto_trigger")

    if new_source ~= source_name or new_scene ~= scene_name then
        layout_captured = false
        is_squeezed = false
    end
    source_name = new_source
    scene_name = new_scene

    -- Auto-capture layout when both fields are set (no manual button needed)
    if not layout_captured and source_name ~= "" and scene_name ~= "" then
        if not is_animating then
            capture_layout()
        end
    end

    obs.obs_frontend_remove_event_callback(on_frontend_event)
    if auto_trigger then
        obs.obs_frontend_add_event_callback(on_frontend_event)
    end
end

function script_load(settings)
    obs.timer_add(on_timer, TIMER_INTERVAL_MS)
    hotkey_trigger_id = obs.obs_hotkey_register_frontend(
        "squeezeback_toggle", "Squeezeback Toggle", trigger_toggle)
    local a = obs.obs_data_get_array(settings, "squeezeback_toggle_hotkey")
    obs.obs_hotkey_load(hotkey_trigger_id, a)
    obs.obs_data_array_release(a)
    obs.script_log(obs.LOG_INFO, "[Squeezeback] Script loaded")
end

function script_save(settings)
    if hotkey_trigger_id then
        local a = obs.obs_hotkey_save(hotkey_trigger_id)
        obs.obs_data_set_array(settings, "squeezeback_toggle_hotkey", a)
        obs.obs_data_array_release(a)
    end
end

function script_unload()
    obs.timer_remove(on_timer)
    obs.obs_frontend_remove_event_callback(on_frontend_event)
    release_anim_ref()
    if layout_captured then restore_designed_layout() end
    obs.script_log(obs.LOG_INFO, "[Squeezeback] Script unloaded")
end
