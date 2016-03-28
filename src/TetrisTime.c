#include <pebble.h>
#include "assert.h"
#include "digit.h"
#include "field.h"
#include "settings.h"
#include "bitmap.h"


// real const
#define STATE_COUNT 5
#define ANIMATION_SPACING_Y (TETRIMINO_MASK_SIZE + 1)
#define MAX_TETRIMINO_AGE_STEPS 3
#define MAX_TETRIMINO_AGE (MAX_TETRIMINO_AGE_STEPS * s_settings[CUSTOM_ANIMATION_TETRIMINO_AGE_STEP_FRAMES])

// debug settings
#define DYNAMIC_ASSEMBLY 0

typedef struct {
    int8_t offset_x;
    int8_t next_offset_x;
    int8_t offset_y;
    
    bool falling;

    int8_t target_value;
    int8_t next_value;
    DigitDef target;
    DigitDef current;
    int8_t current_tetrimino_age[DIGIT_MAX_TETRIMINOS];

    int8_t action_height;
    int8_t vanishing_frame;

    bool restricted_spawn_width;
} DigitState;

// time state
static bool s_show_second_dot = true;
static uint8_t s_month;
static uint8_t s_day;
static uint8_t s_weekday;

// color state
static GColor s_bg_color;
static GColor s_fg_color;

// pebbele infrastructure
static bool s_animating;
static Window* s_window;
static Layer* s_layer;
static bool s_second_draw_hack;

// digit states
static DigitState s_states[STATE_COUNT];
static int8_t s_date_frame;

static void state_step(DigitState* state) {
    if (!state->falling) {
        if (state->next_value != state->target_value || state->next_offset_x != state->offset_x) {
            const int animation_period_frames = s_settings[CUSTOM_ANIMATION_PERIOD_VIS_FRAMES] + s_settings[CUSTOM_ANIMATION_PERIOD_INVIS_FRAMES];
            if (state->vanishing_frame > s_settings[CUSTOM_ANIMATION_PERIOD_COUNT] * animation_period_frames) {
                //APP_LOG(APP_LOG_LEVEL_INFO, "Digit target changed to %d", state->next_value);
                state->target_value = state->next_value;
                state->offset_x = state->next_offset_x;
                if (DYNAMIC_ASSEMBLY) {
                    reorder_digit(&state->target, &s_digits[state->target_value]);
                } else {
                    state->target = s_digits[state->target_value];
                }
                state->current.size = 0;
                state->falling = true;
                state->vanishing_frame = 0;
            } else {
                state->vanishing_frame += 1;
                return;
            }
        } else {
            return;
        }
    }
   
    int last_y = TETRIMINO_MASK_SIZE;
    for (int i = 0; i < state->current.size; ++i) {
        TetriminoPos* current_pos = &state->current.tetriminos[i];
        const TetriminoPos* target_pos = &state->target.tetriminos[i];

        const int height_remaining = target_pos->y - current_pos->y;
        const int moves_needed = abs(target_pos->x - current_pos->x);
        int rotations_needed = target_pos->rotation - current_pos->rotation;
        if (rotations_needed < 0) { rotations_needed += 4; }
        const int actions_needed = moves_needed + rotations_needed;

        if (state->action_height >= current_pos->y) {
            int step = height_remaining / (actions_needed + 1);
            state->action_height = current_pos->y + step;
        }

        if (current_pos->y < target_pos->y) {
            current_pos->y += 1;
        } else {
            if (state->current_tetrimino_age[i] < MAX_TETRIMINO_AGE) {
                state->current_tetrimino_age[i] += 1;
            }
        }

        if (current_pos->y >= state->action_height) {
            if (moves_needed > rotations_needed) {
                if (current_pos->x < target_pos->x) {
                    current_pos->x += 1;
                } else if (current_pos->x > target_pos->x) {
                    current_pos->x -= 1;
                }
            } else if (rotations_needed) {
                current_pos->rotation = (current_pos->rotation + 1) % 4;
            }
        }
     
        last_y = current_pos->y;
    }
    
    
    if (state->current.size < state->target.size) {
        const TetriminoPos* target_pos = &state->target.tetriminos[state->current.size];
        const char target_letter = target_pos->letter;
        const TetriminoDef* td = get_tetrimino_def(target_letter);

        const int start_y = -state->offset_y - td->size + 1;
        if (last_y >= (start_y + ANIMATION_SPACING_Y)) {
            TetriminoPos* current_pos = &state->current.tetriminos[state->current.size];
            current_pos->letter = target_letter;
            if (state->restricted_spawn_width) {
                const int spawn_width = 4;
                current_pos->x = rand() % (spawn_width - td->size + 1);
                current_pos->x += (DIGIT_WIDTH - spawn_width) / 2;
            } else {
                current_pos->x = rand() % (DIGIT_WIDTH - td->size + 1);
            }
            current_pos->y = start_y;
            const int rotation_unique = rand() % td->unique_shapes;
            current_pos->rotation = (target_pos->rotation - rotation_unique + 4) % 4;
            state->action_height = start_y;
            state->current_tetrimino_age[state->current.size] = 0;
            state->current.size += 1;
        }
    }

    if (state->current.size == state->target.size) {
        if (state->current.size == 0 ||
            ((memcmp(&state->current.tetriminos[state->current.size-1],
                    &state->target.tetriminos[state->target.size-1],
                    sizeof(TetriminoPos)) == 0) &&
            (state->current_tetrimino_age[state->current.size-1] >= MAX_TETRIMINO_AGE)))
        {
            state->falling = false;
        }
    }
}

static void draw_weekday_line(int height, GColor color) {
  //here is date font switch gogo  
  //const Bitmap* weekdays = s_settings[LARGE_DATE_FONT] ? s_large_weekdays : s_small_weekdays;
  const Bitmap* weekdays = s_small_weekdays;
    const Bitmap* bmp = &weekdays[s_weekday];
    draw_bitmap(bmp, (FIELD_WIDTH - bmp->width + 1) / 2, height, color);
}

static void draw_marked_weekday_line(int height, GColor color, bool use_letter) {
  //marked weekdays font switch gogo
    //const Bitmap* marked_weekdays = s_settings[LARGE_DATE_FONT] ? s_large_marked_weekdays : s_small_marked_weekdays;
  const Bitmap* marked_weekdays = s_small_marked_weekdays;
    
    const int first_weekday = s_settings[DATE_FIRST_WEEKDAY];
    int width = 0;
    for (int i = 0; i < 7; ++i) {
        const int day = (first_weekday + i) % 7;
        const int bmp_idx = (day == s_weekday) ? (use_letter ? day : 8) : 7;
        width += marked_weekdays[bmp_idx].width;
    }
    int offset = (FIELD_WIDTH - width + 1) / 2;
    for (int i = 0; i < 7; ++i) {
        const int day = (first_weekday + i) % 7;
        const int bmp_idx = (day == s_weekday) ? (use_letter ? day : 8) : 7;
        draw_bitmap_move(&offset, &marked_weekdays[bmp_idx], height, color, 0);
    }
}

static void draw_date_line(int height, GColor color) {
    const DateMonthFormat dmf = s_settings[DATE_MONTH_FORMAT];
    //various font switch gogo
    /*const Bitmap* months = s_settings[LARGE_DATE_FONT] ? s_large_months : s_small_months;
    const Bitmap* weekdays = s_settings[LARGE_DATE_FONT] ? s_large_weekdays : s_small_weekdays;
    const Bitmap* bmp_digits = s_settings[LARGE_DATE_FONT] ? s_bmp_large_digits : s_bmp_small_digits;
    const int bmp_digit_width = s_settings[LARGE_DATE_FONT] ? BMP_LARGE_DIGIT_WIDTH : BMP_SMALL_DIGIT_WIDTH;*/
    
    const Bitmap* months = s_small_months;
    const Bitmap* weekdays = s_small_weekdays;
    const Bitmap* bmp_digits = s_bmp_small_digits;
    const int bmp_digit_width = BMP_SMALL_DIGIT_WIDTH;
  
  
    // digit
    int width = bmp_digit_width;
    if (s_day >= 10) {
        width += 1 + bmp_digit_width;
    }

    const int date_word_spacing = s_settings[CUSTOM_DATE_WORD_SPACING];

    // month
    switch (dmf) {
    case DMF_MONTH_BEFORE:
    case DMF_MONTH_AFTER:
        width += months[s_month].width + date_word_spacing;
        break;
    case DMF_WEEKDAY_BEFORE:
    case DMF_WEEKDAY_AFTER:
        width += weekdays[s_weekday].width + date_word_spacing;
        break;
    default:
        break;
    }

    int offset = (FIELD_WIDTH - width + 1) / 2;
    
    // month before
    if (dmf == DMF_MONTH_BEFORE) {
        draw_bitmap_move(&offset, &months[s_month], height, color, date_word_spacing);
    } else if (dmf == DMF_WEEKDAY_BEFORE) {
        draw_bitmap_move(&offset, &weekdays[s_weekday], height, color, date_word_spacing);
    }

    // date
    if (s_day >= 10) {
        draw_bitmap_move(&offset, &bmp_digits[s_day / 10], height, color, 1);
    }
    draw_bitmap_move(&offset, &bmp_digits[s_day % 10], height, color, date_word_spacing);

    // month after
    if (dmf == DMF_MONTH_AFTER) {
        draw_bitmap_move(&offset, &months[s_month], height, color, date_word_spacing);
    } else if (dmf == DMF_WEEKDAY_AFTER) {
        draw_bitmap_move(&offset, &weekdays[s_weekday], height, color, date_word_spacing);
    }
}

static int get_final_date_split_height() {
    return s_states[0].offset_y + DIGIT_HEIGHT + s_settings[CUSTOM_TIME_DATE_SPACING_1];
}

static void draw_date() {
    const DateMode dm = s_settings[DATE_MODE];
    if (dm == DM_NONE) {
        return;
    }

    const int date_period_frames = s_settings[CUSTOM_ANIMATION_DATE_PERIOD_FRAMES];
    const int split_height = get_final_date_split_height() + (s_date_frame + date_period_frames - 1) / date_period_frames;

    GColor date_color;
    if (dm == DM_INVERTED) {
        date_color = s_bg_color;
        for (int j = split_height; j < FIELD_HEIGHT; ++j) {
            for (int i = 0; i < FIELD_WIDTH; ++i) {
                field_draw(i, j, s_fg_color);
            }
        }
    } else {
        date_color = s_fg_color;
    }

    const int bmp_height = BMP_SMALL_HEIGHT;
    const int first_line_height = split_height + s_settings[CUSTOM_TIME_DATE_SPACING_2];
    const int second_line_height = first_line_height + bmp_height + s_settings[CUSTOM_DATE_LINE_SPACING];
    const DateWeekdayFormat dwf = s_settings[DATE_WEEKDAY_FORMAT];

    draw_date_line(first_line_height, date_color);
    switch(dwf) {
    case DWF_MARKED:
        draw_marked_weekday_line(second_line_height-1, date_color, 0);
        break;
    case DWF_LETTER:
        draw_marked_weekday_line(second_line_height, date_color, 1);
        break;
    case DWF_TEXT:
        draw_weekday_line(second_line_height, date_color);
        break;
    default:
        break; // nothing
    }
}

static inline int _min(int a, int b) {
    return a < b ? a : b;
}

static inline int _step(int current, int target, int max_step) {
    if (current > target) return current - _min(max_step, current-target);
    if (current < target) return current + _min(max_step, target-current);
    return current;
}

static void draw_tetrimino(const TetriminoPos* tp, int offset_x, int offset_y, int age) {
    const TetriminoDef* td = get_tetrimino_def(tp->letter); 
    const TetriminoMask* tm = &td->rotations[tp->rotation];

    GColor color = s_fg_color;
    
    #ifdef PBL_COLOR
    if (age < MAX_TETRIMINO_AGE) {
        color = BYTE_TO_COLOR(td->color);
        const int age_step = age / s_settings[CUSTOM_ANIMATION_TETRIMINO_AGE_STEP_FRAMES];

        if (age_step) {
            color.r = _step(color.r, s_fg_color.r, age_step);
            color.g = _step(color.g, s_fg_color.g, age_step);
            color.b = _step(color.b, s_fg_color.b, age_step);
        }
    }
    #endif

    for (int mask_y = 0; mask_y < TETRIMINO_MASK_SIZE; ++mask_y) {
        for (int mask_x = 0; mask_x < TETRIMINO_MASK_SIZE; ++mask_x) {
            if ((*tm)[mask_y][mask_x]) {
                const int x = tp->x + mask_x + offset_x;
                const int y = tp->y + mask_y + offset_y;
                field_draw(x, y, color);
            }
        }
    }
}

static void draw_digit_def(const DigitDef* def, int offset_x, int offset_y, const int8_t* ages) {
    for (int i = 0; i < def->size; ++i) {
        draw_tetrimino(&def->tetriminos[i], offset_x, offset_y, ages[i]);
    }
}

static void draw_digit_state(const DigitState* state) {
    if (state->vanishing_frame) {
        const int animation_period_frames = s_settings[CUSTOM_ANIMATION_PERIOD_VIS_FRAMES] + s_settings[CUSTOM_ANIMATION_PERIOD_INVIS_FRAMES];
        const int in_period = (state->vanishing_frame - 1) % animation_period_frames;
        if (in_period < s_settings[CUSTOM_ANIMATION_PERIOD_INVIS_FRAMES]) {
            return;
        }
    }
    draw_digit_def(&state->current, state->offset_x, state->offset_y, state->current_tetrimino_age);
}

static void draw_digit_state_directy(Layer* layer, GContext* ctx, const DigitState* state, GColor color) {
    for (int t = 0; t < state->current.size; ++t) {
        const TetriminoPos* tp = &state->current.tetriminos[t];
        const TetriminoDef* td = get_tetrimino_def(tp->letter);
        const TetriminoMask* tm = &td->rotations[tp->rotation];
        for (int mask_y = 0; mask_y < TETRIMINO_MASK_SIZE; ++mask_y) {
            for (int mask_x = 0; mask_x < TETRIMINO_MASK_SIZE; ++mask_x) {
                if ((*tm)[mask_y][mask_x]) {
                    const int x = tp->x + mask_x + state->offset_x;
                    const int y = tp->y + mask_y + state->offset_y;
                    field_direct_draw(layer, ctx, x, y, color);
                }
            }
        }
    }
}

static void layer_draw(Layer* layer, GContext* ctx) {
    if (s_second_draw_hack) {
        GColor second_color = s_show_second_dot ? s_fg_color : s_bg_color;
        draw_digit_state_directy(layer, ctx, &s_states[4], second_color);
        s_second_draw_hack = false;
        return;
    }
    for (int i = 0; i < 4; ++i) {
        draw_digit_state(&s_states[i]);
    }
    if (s_show_second_dot || s_states[4].falling || s_states[4].vanishing_frame) {
        draw_digit_state(&s_states[4]);
    }
    draw_date();
    if (s_settings[ICON_CONNECTION]) {
        if (!bluetooth_connection_service_peek()) {
            draw_bitmap(&s_bluetooth, 0, 0, s_fg_color);
        }
    }
  //battery icon!
    if (s_settings[ICON_BATTERY]) {
        const Bitmap* bmp = NULL;
        BatteryChargeState charge_state = battery_state_service_peek();
        if (charge_state.is_charging) {
            bmp = &s_battery_charging;
        } else if (charge_state.charge_percent <= 10) {
            bmp = &s_battery_empty;
        } else if (charge_state.charge_percent <= 20) {
            bmp = &s_battery_halfempty;
        }
        if (bmp) {
            draw_bitmap(bmp, FIELD_WIDTH - bmp->width, 0, s_fg_color);
        }
    }
    
    field_flush(layer, ctx);
}

static int is_animating() {
    if (s_date_frame) {
        return 1;
    }
    for (int i = 0; i < STATE_COUNT; ++i) {
        if (s_states[i].falling || s_states[i].vanishing_frame) {
            return 1;
        }
    }
    return 0;
}

static void process_animation(void* data) {
    /*
    static int st = 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "Step %d", st);
    st += 1;
    */
    
    s_animating = true;
    if (s_date_frame) {
        s_date_frame -= 1;
    }
    for (int i = 0; i < STATE_COUNT; ++i) {
        state_step(&s_states[i]);
    }
    layer_mark_dirty(s_layer);
    if (is_animating()) {
        app_timer_register(s_settings[CUSTOM_ANIMATION_TIMEOUT_MS], process_animation, NULL);
    } else {
        s_animating = false;
    }
}

inline static void notify(NotificationType notification) {
    switch (notification) {
    case NTF_SHORT_PULSE:
        vibes_short_pulse();
        break;
    case NTF_LONG_PULSE:
        vibes_long_pulse();
        break;
    case NTF_DOUBLE_PULSE:
        vibes_double_pulse();
        break;
    default:
        break;
    }
}

static void bt_handler(bool connected) {
    if (s_layer && s_settings[ICON_CONNECTION]) {
        layer_mark_dirty(s_layer);
    }
    if (connected) {
        notify(s_settings[NOTIFICATION_CONNECTED]);
    } else {
        notify(s_settings[NOTIFICATION_DISCONNECTED]);
    }
}

static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
    if (units_changed & DAY_UNIT) {
        s_month = tick_time->tm_mon;
        s_day = tick_time->tm_mday;
        s_weekday = tick_time->tm_wday;
        //s_weekday = (tick_time->tm_sec / 2) % 7;
        //s_month = ((tick_time->tm_sec + 1) / 2) % 12;
    }

    if (units_changed & HOUR_UNIT) {
        if (units_changed != (TimeUnits)(-1)) {
            notify(s_settings[NOTIFICATION_HOURLY]);
        }
    }
    
    if (units_changed & MINUTE_UNIT) {
        const int clock24 = clock_is_24h_style();
    
        int digit_values[STATE_COUNT];
        int digit_offsets[STATE_COUNT];
        int hour = tick_time->tm_hour;
    
        if (!clock24) {
            hour = hour % 12;
            if (hour == 0) {
                hour = 12;
            }
        }
    
        digit_values[0] = hour / 10;
        digit_values[1] = hour % 10;
        digit_values[2] = tick_time->tm_min / 10;
        digit_values[3] = tick_time->tm_min % 10;
        digit_values[4] = 10;

        if (!clock24) {
            if (digit_values[0] == 0) {
                digit_values[0] = DIGIT_COUNT;

                digit_offsets[0] = 0;
                digit_offsets[1] = 5;
                digit_offsets[2] = 17;
                digit_offsets[3] = 25;
                digit_offsets[4] = 11;

                /*
                  if (digit_values[1] == 1) {
                  for (int i = 0; i < STATE_COUNT; ++i) {
                  s_states[i].next_offset_x -= 1;
                  }
                  }
                */
            } else {
                digit_offsets[0] = 0;
                digit_offsets[1] = 8;
                digit_offsets[2] = 20;
                digit_offsets[3] = 28;
                digit_offsets[4] = 14;
            }
        } else {
            digit_offsets[0] = 2;
            digit_offsets[1] = 10;
            digit_offsets[2] = 20;
            digit_offsets[3] = 28;
            digit_offsets[4] = 15;
        }
    
        bool changed = false;
        for (int i = 0; i < STATE_COUNT; ++i) {
            const int value = digit_values[i];
            const int offset = digit_offsets[i];
            if (s_states[i].next_value != value || s_states[i].next_offset_x != offset) {
                s_states[i].next_value = value;
                s_states[i].next_offset_x = offset;
                //APP_LOG(APP_LOG_LEVEL_INFO, "Digit %d scheduled to be %d", i, value);
                changed = true;
            }
        }

        if (changed && !s_animating) {
            process_animation(NULL);
        }
    }

    if (units_changed & SECOND_UNIT) {
        if (s_settings[ANIMATE_SECOND_DOT]) {
            s_show_second_dot = tick_time->tm_sec % 2;
            s_second_draw_hack = !s_animating;
            layer_mark_dirty(s_layer);
        }
    }
}

static void battery_handler(BatteryChargeState charge_state) {
    if (s_layer) {
        layer_mark_dirty(s_layer);
    }
}

static void on_settings_changed() {
    int offset_y = (FIELD_HEIGHT - DIGIT_HEIGHT) / 2;
    if (s_settings[DATE_MODE] != DM_NONE) {
        offset_y -= s_settings[CUSTOM_TIME_OFFSET];
    }
    for (int i = 0; i < STATE_COUNT; ++i) {
        s_states[i].offset_y = offset_y;
    }

    if (s_settings[LIGHT_THEME]) {
        s_bg_color = GColorWhite;
        s_fg_color = GColorBlack;
    } else {
        s_bg_color = GColorBlack;
        s_fg_color = GColorWhite;
    }

    if (!s_settings[SKIP_INITIAL_ANIMATION]) {
        s_date_frame = (FIELD_HEIGHT - get_final_date_split_height()) * s_settings[CUSTOM_ANIMATION_DATE_PERIOD_FRAMES];
    } else {
        s_date_frame = 0;
    }

    tick_timer_service_unsubscribe();
    if (s_settings[ANIMATE_SECOND_DOT]) {
        tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    } else {
        s_show_second_dot = true;
        tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    }

    if (s_settings[ICON_CONNECTION] || s_settings[NOTIFICATION_DISCONNECTED] || s_settings[NOTIFICATION_CONNECTED]) {
        bluetooth_connection_service_subscribe(bt_handler);
    } else {
        bluetooth_connection_service_unsubscribe();
    }

    if (s_settings[ICON_BATTERY]) {
        battery_state_service_subscribe(battery_handler);
    } else {
        battery_state_service_unsubscribe();
    }

    field_reset(s_bg_color);
    if (s_layer) {
        layer_mark_dirty(s_layer);
    }
    if (!s_animating) {
        process_animation(NULL);
    }
}

static void in_received_handler(DictionaryIterator* iter, void* context)
{
    settings_read(iter);
    on_settings_changed();
}

static void main_window_load(Window* window) {
    s_layer = window_get_root_layer(window);
    layer_set_update_proc(s_layer, layer_draw);

    on_settings_changed();

    time_t now;
    time(&now);
    struct tm* now_time = localtime(&now);
    tick_handler(now_time, -1);

    if (s_settings[SKIP_INITIAL_ANIMATION]) {
        for (int i = 0; i < STATE_COUNT; ++i) {
            // skip vanishing animation
            const int animation_period_frames = s_settings[CUSTOM_ANIMATION_PERIOD_VIS_FRAMES] + s_settings[CUSTOM_ANIMATION_PERIOD_INVIS_FRAMES];
            s_states[i].vanishing_frame = s_settings[CUSTOM_ANIMATION_PERIOD_COUNT] * animation_period_frames + 1;
            state_step(&s_states[i]);
            s_states[i].current = s_states[i].target;
            for (int j = 0; j < s_states[i].current.size; ++j) {
                s_states[i].current_tetrimino_age[j] = MAX_TETRIMINO_AGE;
            }
        }
    }
}

static void main_window_unload(Window* window) {
    s_layer = NULL;
}
  
static void init() {
    srand(time(NULL));

    app_message_register_inbox_received(in_received_handler);
    AppMessageResult rc = app_message_open(256, 256);
    ASSERT2(rc == APP_MSG_OK, "app_message_open => %d", (int)rc);
    
    settings_load_persistent();
    
#if USE_RAW_DIGITS == 1
    bitmap_check_all();
    for (int i = 0; i < DIGIT_COUNT; ++i) {
        DigitDef def;
        if (!parse_raw_digit(&def, &s_raw_digits[i])) {
            return;
        }
        reorder_digit(&s_digits[i], &def);
    }
    /*
    for (int i = 0; i < DIGIT_COUNT; ++i) {
        format_digit_def_struct(&s_digits[i]);
    }
    */
#endif

    for (int i = 0; i < STATE_COUNT; ++i) {
        s_states[i].next_value = -1;
        s_states[i].target_value = -1;
        // it looks better WITH vanishing animation
        //s_states[i].vanishing_frame = s_settings[CUSTOM_ANIMATION_PERIOD_COUNT] * ANIMATION_PERIOD_FRAMES + 1;
    }
    s_states[4].restricted_spawn_width = true;
    
    // init window
    s_window = window_create();
    ASSERT(s_window);
    window_set_window_handlers(s_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload });
    window_stack_push(s_window, true);
}

static void deinit() {
    window_destroy(s_window);
    s_window = NULL;
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
