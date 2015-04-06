#include "pebble.h"

#define SETTINGS_VERSION_VALUE 1

// I should've used a prefix =/
typedef enum {
    VERSION = 0,
    LIGHT_THEME,
    ANIMATE_SECOND_DOT,
    DIGITS_MODE,

    DATE_MODE,
    DATE_MONTH_FORMAT,
    DATE_WEEKDAY_FORMAT,
    DATE_FIRST_WEEKDAY,

    ICON_CONNECTION,
    ICON_BATTERY,
    
    NOTIFICATION_DISCONNECTED,
    NOTIFICATION_CONNECTED,

    SKIP_INITIAL_ANIMATION,
    NOTIFICATION_HOURLY,

    LARGE_DATE_FONT,

    CUSTOM_DATE,
    CUSTOM_TIME_OFFSET,
    CUSTOM_TIME_DATE_SPACING_1,
    CUSTOM_TIME_DATE_SPACING_2,
    CUSTOM_DATE_WORD_SPACING,
    CUSTOM_DATE_LINE_SPACING,

    CUSTOM_ANIMATIONS,
    CUSTOM_ANIMATION_TIMEOUT_MS,
    CUSTOM_ANIMATION_PERIOD_VIS_FRAMES,
    CUSTOM_ANIMATION_PERIOD_INVIS_FRAMES,
    CUSTOM_ANIMATION_PERIOD_COUNT,
    CUSTOM_ANIMATION_DATE_PERIOD_FRAMES,
    
    MAX_KEY,
} SettingsKey;

typedef enum {
    DM_ASYMMETRIC,
    DM_DENSE,
    DM_SPARSE,
    DIM_MAX,
} DigitsMode;

typedef enum {
    DM_INVERTED,
    DM_SAME_COLOR,
    DM_NONE,
    DM_MAX,
} DateMode;

typedef enum {
    DMF_MONTH_BEFORE,
    DMF_MONTH_AFTER,
    DMF_WEEKDAY_BEFORE,
    DMF_WEEKDAY_AFTER,
    DMF_MAX,
} DateMonthFormat;

typedef enum {
    DWF_MARKED,
    DWF_LETTER,
    DWF_TEXT,
    DWF_NO_WEEKDAY,
    DWF_MAX,
} DateWeekdayFormat;

typedef enum {
    NTF_NONE,
    NTF_SHORT_PULSE,
    NTF_LONG_PULSE,
    NTF_DOUBLE_PULSE,
    NTF_MAX,
} NotificationType;

typedef int Settings[MAX_KEY];

static Settings s_settings;

static int settings_get_default(SettingsKey key) {
    switch (key) {
    case DATE_WEEKDAY_FORMAT:
        return DWF_TEXT;
    case NOTIFICATION_DISCONNECTED:
        return NTF_DOUBLE_PULSE;
    default:
        return 0;
    }
}

static void settings_apply(const int* new_settings) {
    for (int i = 0; i < MAX_KEY; ++i) {
        if (new_settings[i] < 0) {
            s_settings[i] = settings_get_default(i);
        } else {
            s_settings[i] = new_settings[i];
        }
    }
    
    s_settings[VERSION] = SETTINGS_VERSION_VALUE;
    s_settings[LIGHT_THEME] %= 2;
    s_settings[ANIMATE_SECOND_DOT] %= 2;
    s_settings[DIGITS_MODE] %= DIM_MAX;
    s_settings[DATE_MODE] %= DM_MAX;
    s_settings[DATE_MONTH_FORMAT] %= DMF_MAX;
    s_settings[DATE_WEEKDAY_FORMAT] %= DWF_MAX;
    s_settings[DATE_FIRST_WEEKDAY] %= 7;
    s_settings[ICON_CONNECTION] %= 2;
    s_settings[ICON_BATTERY] %= 2;
    s_settings[NOTIFICATION_DISCONNECTED] %= NTF_MAX;
    s_settings[NOTIFICATION_CONNECTED] %= NTF_MAX;

    // disable duplicated text weekday
    if (s_settings[DATE_WEEKDAY_FORMAT] == DWF_TEXT) {
        const DateMonthFormat dmf = s_settings[DATE_MONTH_FORMAT];
        if (dmf == DMF_WEEKDAY_BEFORE || dmf == DMF_WEEKDAY_AFTER) {
            s_settings[DATE_WEEKDAY_FORMAT] = DWF_NO_WEEKDAY;
        }
    }

    // fix custom values
    if (!s_settings[CUSTOM_DATE]) {
        if (s_settings[DATE_MODE] != DM_NONE) {
            const int large_font = s_settings[LARGE_DATE_FONT];
            switch (s_settings[DATE_WEEKDAY_FORMAT]) {
            case DWF_MARKED:
                s_settings[CUSTOM_TIME_OFFSET] = large_font ? 2 : 1;
                break;
            case DWF_LETTER:
                s_settings[CUSTOM_TIME_OFFSET] = large_font ? 3 : 2;
                break;
            case DWF_TEXT:
                s_settings[CUSTOM_TIME_OFFSET] = large_font ? 4 : 2;
                break;
            default:
            case DWF_NO_WEEKDAY:
                s_settings[CUSTOM_TIME_OFFSET] = 1;
                break;
            }
        } else {
            s_settings[CUSTOM_TIME_OFFSET] = 0;
        }
        s_settings[CUSTOM_TIME_DATE_SPACING_1] = 2;
        s_settings[CUSTOM_TIME_DATE_SPACING_2] = 2;
        s_settings[CUSTOM_DATE_WORD_SPACING] = 3;
        s_settings[CUSTOM_DATE_LINE_SPACING] = 2;
    }

    if (!s_settings[CUSTOM_ANIMATIONS]) {
        s_settings[CUSTOM_ANIMATION_TIMEOUT_MS] = 100;
        s_settings[CUSTOM_ANIMATION_PERIOD_VIS_FRAMES] = 2;
        s_settings[CUSTOM_ANIMATION_PERIOD_INVIS_FRAMES] = 1;
        s_settings[CUSTOM_ANIMATION_PERIOD_COUNT] = 3;
        s_settings[CUSTOM_ANIMATION_DATE_PERIOD_FRAMES] = 4;
    }
}

static void settings_load_persistent() {
    Settings new_settings;
    for (int i = 0; i < MAX_KEY; ++i) {
        new_settings[i] = persist_exists(i) ? persist_read_int(i) : -1;
    }
    settings_apply(new_settings);
}

static void settings_save_persistent() {
    for (int i = 0; i < MAX_KEY; ++i) {
        persist_write_int(i, s_settings[i]);
    }
}

static void settings_send() {
    DictionaryIterator* it;
    app_message_outbox_begin(&it);

    for (int i = 0; i < MAX_KEY; ++i) {
        dict_write_int(it, i, &s_settings[i], 4, 1);
    }
 
    app_message_outbox_send();
}

static void settings_read(DictionaryIterator* iter) 
{
    Settings new_settings;
    memcpy(&new_settings, &s_settings, sizeof(Settings));
    
    Tuple* t = dict_read_first(iter);
    while (t) {
        if (t->key < MAX_KEY) {
            switch (t->type) {
            case TUPLE_UINT:
            case TUPLE_INT:
                new_settings[t->key] = t->value->int8;
                break;
            default:
                APP_LOG(APP_LOG_LEVEL_ERROR, "Unexpected key type: %d:%d", (int)t->key, (int)t->type);
            }
        } else {
            APP_LOG(APP_LOG_LEVEL_ERROR, "Unexpected key: %d", (int)t->key);
        }
        t = dict_read_next(iter);
    }

    settings_apply(new_settings);
    settings_save_persistent();
}
