#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/dolphin.h>
#include <furi_hal.h>
#include <furi_hal_speaker.h>
#include <furi_hal_vibro.h>
#include "stm32_sam.h"

// Constants for screen and game mechanics
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define PORTRAIT_WIDTH 64
#define PORTRAIT_HEIGHT 128
#define FPS_BASE 22
#define MAX_STREAK_INT 9999 // Arbitrary max for streak to handle overflow
#define COOLDOWN_MS 180000 // 3 minutes
#define NOTIFICATION_MS 1500 // 1.5 seconds
#define FIXED_POINT_SCALE 1000 // Scale for fixed-point arithmetic
#define BACK_BUTTON_COOLDOWN 500 // 500ms cooldown for Back button
#define ORIENTATION_HOLD_MS 1500 // 1.5s for orientation toggle
#define CREDITS_FPS 11700 // 11.7 FPS = 85ms per frame
#define LOADING_MS 1500 // 1.5s loading screen
#define TAP_DRM_MS 300 // 0.3s for tap DRM
#define MIN_SPEED_BPM 65 // Minimum speed for speed bar
#define SPEED_BAR_Y (PORTRAIT_HEIGHT - 8)
#define SPEED_BAR_HEIGHT 2
#define SPEED_BAR_X 0
#define SPEED_BAR_WIDTH PORTRAIT_WIDTH

// Global limit for objects across games
#define WORLD_OBJ_LIMIT 8 // Comment: Adjust for performance tuning

// Game states for the mini-game suite
typedef enum {
    GAME_STATE_LOADING, // Initial loading screen
    GAME_STATE_TITLE,
    GAME_STATE_ROTATE,
    GAME_STATE_ZERO_HERO,
    GAME_STATE_FLIP_ZIP,
    GAME_STATE_LINE_CAR, // Racing simulator
    GAME_STATE_FLIP_IQ,  // IQ-based game (replaces Drop Per)
    GAME_STATE_TECTONE_SIM, // Streamer simulator
    GAME_STATE_SPACE_FLIGHT, // Space flight game
    GAME_STATE_CREDITS,
    GAME_STATE_PAUSE
} GameState;

// Game modes for menu selection
typedef enum {
    GAME_MODE_ZERO_HERO,
    GAME_MODE_FLIP_ZIP,
    GAME_MODE_LINE_CAR,
    GAME_MODE_FLIP_IQ,     // Replaces Drop Per
    GAME_MODE_TECTONE_SIM,
    GAME_MODE_SPACE_FLIGHT
} GameMode;

// Difficulty levels
typedef enum {
    DIFFICULTY_EASY,
    DIFFICULTY_MEDIUM,
    DIFFICULTY_HARD
} Difficulty;

// Game context structure to hold all game states and variables
typedef struct {
    GameState state;
    GameMode selected_game;
    bool is_left_handed;
    uint32_t last_input_time;
    uint8_t rapid_click_count;
    uint32_t game_start_time;
    bool is_day;
    uint32_t day_night_toggle_time;
    // Title menu
    int selected_side; // 0: left, 1: right
    int selected_row; // 0: row1, 1: row2, 2: row3
    int title_scroll_offset; // For scrolling menu
    uint32_t back_hold_start; // Track back button hold time
    // Rotate animation
    uint32_t rotate_start_time;
    int32_t rotate_angle; // Fixed-point (degrees * FIXED_POINT_SCALE)
    int32_t zoom_factor; // Fixed-point (scale * FIXED_POINT_SCALE)
    bool rotate_skip;
    // Zero Hero
    int streak;
    int prev_streak;
    int highest_streak;
    int streak_sum;
    int streak_count;
    int oflow;
    Difficulty difficulty;
    uint32_t last_difficulty_check;
    int key_columns[5][WORLD_OBJ_LIMIT]; // U, L, O, R, D - Adjusted to 2D array for Flip IQ balls
    int key_positions[5][10]; // Up to 10 keys per column
    bool is_holding[5];
    bool strum_hit[5]; // Highlight strumming bar on hit
    int score;
    int score_oflow;
    uint32_t last_notification_time;
    char notification_text[32];
    uint8_t note_q_a; // 0: none, 1: YES, 2: NO
    int notification_x; // Scrolling position for notifications
    // Flip Zip
    int mascot_lane; // 0 to 4
    int mascot_y; // Vertical position in lanes plane
    int speed_bpm;
    uint32_t last_back_press;
    bool is_jumping;
    int32_t jump_progress; // Fixed-point (progress * FIXED_POINT_SCALE)
    int jump_scale; // Grow/shrink during jump
    uint32_t jump_hold_time; // Track OK button hold duration
    int successful_jumps; // Count for speed increases
    int obstacles[5][10]; // Obstacle type per lane
    int obstacle_positions[5][10];
    uint32_t last_tap_time; // For tap DRM and speed boost
    int tap_count; // Track taps for BPM calculation
    uint32_t tap_window_start; // Start of tap window for BPM
    int jump_y_accumulated; // Track Up presses during jump
    // Line Car
    int car_lane; // Current lane (0-4)
    int car_y; // Vertical position
    int car_angle; // Rotation angle (0, 8, 15 degrees) - Simplified to offset instead of rotation
    int prev_car_lane; // Track previous lane for drift comparison
    int track_pieces[5][WORLD_OBJ_LIMIT]; // Lengths of track pieces per lane
    int track_positions[5][WORLD_OBJ_LIMIT]; // Positions of track pieces
    int uber_points; // Skill points from drifting
    int drift_multiplier; // Multiplier for successful drifts
    uint32_t last_drift_time; // Timer for drift duration (693ms)
    bool is_drifting; // Drift state
    int fast_line; // 20 pixels below UI (26 + 20 = 46)
    int slow_line; // 20 pixels above marquee (128 - 7 - 20 = 101)
    // Flip IQ
    int ball_width; // Width of initial drop ball
    uint32_t round_start_time; // Timer for round duration
    bool floor_check_flag; // Flag for floor removal
    int active_lanes; // Number of active lanes (5 to 2)
    int ball_count; // Total balls to drop per round
    int balls_on_screen[WORLD_OBJ_LIMIT]; // Positions of balls
    int ball_sizes[WORLD_OBJ_LIMIT]; // Sizes of balls
    bool ball_broken[WORLD_OBJ_LIMIT]; // Broken state
    // Tectone Sim
    int anger; // Emotion levels (0-9)
    int based; // Emotion levels (0-9)
    int cuteness; // Emotion levels (0-9)
    int sad; // Emotion levels (0-9)
    uint32_t emotion_cooldown; // Cooldown for emotion actions
    int tectone_x; // X position in bedroom
    uint32_t move_cooldown; // Time between movements
    uint32_t last_move_time; // Last movement time
    int comment_heights[WORLD_OBJ_LIMIT]; // Heights of comments
    int comment_positions[WORLD_OBJ_LIMIT]; // Y positions of comments
    bool hype_train[WORLD_OBJ_LIMIT]; // Hype train state
    uint32_t hype_cooldown; // Hype train cooldown
    // Space Flight
    int ship_health; // Player health (9-199)
    int ship_armor; // Player armor (19-99)
    int screen_type; // Current view type (forward, upward, etc.)
    int objects[WORLD_OBJ_LIMIT][3]; // [x, y, size] for objects
    uint32_t last_sequence_time; // Cooldown for special sequences
    int recent_inputs[5]; // Track last 5 inputs
    // Common
    ViewPort* view_port;
    bool should_exit;
    uint32_t last_back_press_time;
    uint32_t last_ai_update; // For faux-multithreading
    uint8_t frame_counter; // For faux-multithreading
    uint8_t start_back_count; // For title menu back count
    uint8_t pause_back_count; // For pause menu back count
    int credits_y; // For credits scrolling
    uint8_t ai_beat_counter; // Added for AI-driven updates
} GameContext;

// SAM Text-to-Speech instance
#if USE_SAM_TTS
static STM32SAM voice;
#endif // USE_SAM_TTS

// Static data for credits, notifications, and menu
static const char* credits_lines[] = {
    "", "Nah2-Nah3", "    ", "    ", "Nah Nah Nah", "    ", "   ", "to the", "    ", "    ", "Nah", ""
};
static const char* notification_messages[] = {
    "Whoa!", "Is it hot or just you?", "Your fingers are lit", "GO GO GO", "You Got This!", "Positive Statement!",
    "Keep Rocking!", "You're on Fire!", "Smash It!", "Unstoppable!", "Epic Moves!"
};
// New notification messages for Line Car
static const char* line_car_notifications[] = {
    "+1 Uber Point Awarded!!!", "%d UP so far!", "OOF, Off Track", "Just lost %d UP!!!"
};
// New notification messages for Flip IQ (5 positive, 5 negative)
static const char* flip_iq_notifications_positive[] = {
    "Great Dodge!", "Nice Climb!", "IQ Rising!", "Sharp Move!", "Genius Play!"
};
static const char* flip_iq_notifications_negative[] = {
    "Ouch, Stumble!", "Missed That!", "IQ Drop!", "Careful Now!", "Fell Behind!"
};
// Tectone Sim comment sections
static const char* tectone_starters[] = {"You know tec ", "Whoa! ", "1", "&%#!@ "};
static const char* tectone_subjects[] = {"BRO ", "look at her ", "he didn't ", "%#!@ "};
static const char* tectone_climaxes[] = {"but it is ", "OMG ", "  ...  ", "is this real ", "that's it"};
static const char* tectone_endpoints[] = {" D-O-N-E", "!!!!!!!", "$%!@$", "BOOM!", "YES"};
static const char* tectone_emotion_phrases[][4] = {
    {"I don't know boys, I didn't say it.", "That's a One Boys", "1 in chat", "Don't let the other side base you down brothers"},
    {"Well, I am six-six chunky hunky", "Look right here", ";-)", "<3"},
    {"Slams Desk", "Cursing", "Ranting", "Beep Sounds"},
    {"Repeats Based", "Pumps Gun", "Eyebrows Up", "Points Up"}
};

static const char* menu_titles[][2] = {
    {"Zero Hero", "Flip Zip"},
    {"Line Car", "Flip IQ"},
    {"Tectone Sim", "Space Flight"}
};
static const char* menu_subtitles[][2] = {
    {"Jamin Banin", "Runin & Jumpin"},
    {"Drift or Nah", "Flip Your IQ"},
    {"Based Hits", "Star Chase"}
};

// Word-wrap text without strtok, safe for Flipper Zero’s limited stdlib
static void draw_word_wrapped_text(Canvas* canvas, const char* text, int x, int y, int max_width, Font font) {
    if(!canvas || !text) return; // Prevent null pointer crashes
    char buffer[32];
    int buffer_idx = 0;
    int current_x = x;
    int current_y = y;
    size_t text_len = strlen(text);
    int char_width = (font == FontPrimary) ? 8 : 6;

    canvas_set_font(canvas, font);
    for(size_t i = 0; i <= text_len; i++) {
        if(text[i] == ' ' || text[i] == '\0' || buffer_idx >= (int)(sizeof(buffer) - 1)) {
            if(buffer_idx > 0) {
                buffer[buffer_idx] = '\0';
                int word_width = buffer_idx * char_width;
                if(current_x + word_width > x + max_width) {
                    current_x = x;
                    current_y += (font == FontPrimary) ? 10 : 8;
                }
                canvas_draw_str(canvas, current_x, current_y, buffer);
                current_x += word_width + char_width;
                buffer_idx = 0;
            }
        } else {
            buffer[buffer_idx++] = text[i];
        }
    }
    canvas_set_font(canvas, FontSecondary);
}

// Draw notifications with scrolling support
static void draw_notification(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx || ctx->notification_text[0] == '\0') return;
    uint32_t elapsed = furi_get_tick() - ctx->last_notification_time;
    if(elapsed > NOTIFICATION_MS && ctx->note_q_a == 0) {
        ctx->notification_text[0] = '\0';
        ctx->notification_x = 0;
        return;
    }
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, PORTRAIT_HEIGHT - 7, PORTRAIT_WIDTH, 7);
    canvas_set_color(canvas, ColorWhite);
    int text_width = strlen(ctx->notification_text) * 6;
    int x = (ctx->note_q_a == 0) ? ctx->notification_x : (PORTRAIT_WIDTH - text_width) / 2;
    draw_word_wrapped_text(canvas, ctx->notification_text, x, PORTRAIT_HEIGHT - 1, PORTRAIT_WIDTH, FontSecondary);
}

// Draw title menu with scrolling support (limited to one row at a time)
static void draw_title_menu(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_line(canvas, SCREEN_WIDTH / 2, 0, SCREEN_WIDTH / 2, SCREEN_HEIGHT);

    int row = ctx->selected_row; // Display only the selected row
    int y_offset = 3; // Fixed offset to center the single row

    // Left option
    if(ctx->selected_side == 0) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_frame(canvas, 1, y_offset + 11, SCREEN_WIDTH / 2 - 2, 30);
        canvas_draw_frame(canvas, 0, y_offset + 10, SCREEN_WIDTH / 2, 32);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 2, y_offset + 12, SCREEN_WIDTH / 2 - 4, 28);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, 1, y_offset + 11, SCREEN_WIDTH / 2 - 2, 30);
    }
    canvas_draw_str(canvas, 10, y_offset + 8, menu_titles[row][0]);
    draw_word_wrapped_text(canvas, menu_subtitles[row][0], 10, y_offset + 50, SCREEN_WIDTH / 2 - 20, FontSecondary);
    if(ctx->selected_side == 0) {
        if(row == 0) { // Zero Hero
            for(int i = 0; i < 5; i++) {
                int x = 3 + i * 10;
                int y = y_offset + 12 + (furi_get_tick() / 100) % 30;
                canvas_draw_str(canvas, x, y, "v");
            }
        } else if(row == 1) { // Line Car
            int x = 10 + (furi_get_tick() / 100) % 30;
            canvas_draw_str(canvas, x, y_offset + 30, "'.-.\\");
        } else { // Tectone Sim
            int frame = (furi_get_tick() / 200) % 2;
            canvas_draw_str(canvas, 16, y_offset + 28, frame == 0 ? "(o_|o)!" : " !(0 |o)");
            canvas_draw_str(canvas, 9, y_offset + 35, frame == 0 ? "/| " : " .-.");
            canvas_draw_str(canvas, 39, y_offset + 35, frame == 0 ? " ,-." : " |\\");
        }
    }

    // Right option
    if(ctx->selected_side == 1) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_frame(canvas, SCREEN_WIDTH / 2 + 1, y_offset + 11, SCREEN_WIDTH / 2 - 2, 30);
        canvas_draw_frame(canvas, SCREEN_WIDTH / 2, y_offset + 10, SCREEN_WIDTH / 2, 32);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, SCREEN_WIDTH / 2 + 2, y_offset + 12, SCREEN_WIDTH / 2 - 4, 28);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, SCREEN_WIDTH / 2 + 1, y_offset + 11, SCREEN_WIDTH / 2 - 2, 30);
    }
    canvas_draw_str(canvas, SCREEN_WIDTH / 2 + 10, y_offset + 8, menu_titles[row][1]);
    draw_word_wrapped_text(canvas, menu_subtitles[row][1], SCREEN_WIDTH / 2 + 10, y_offset + 50, SCREEN_WIDTH / 2 - 20, FontSecondary);
    if(ctx->selected_side == 1) {
        if(row == 0) { // Flip Zip
            int x = SCREEN_WIDTH / 2 + 3 + (furi_get_tick() / 100) % 30;
            canvas_draw_str(canvas, x, y_offset + 30, "F");
        } else if(row == 1) { // Flip IQ
            for(int i = 0; i < 5; i++) {
                int x = SCREEN_WIDTH / 2 + 3 + i * 8;
                int y = y_offset + 12 + (furi_get_tick() / 100 + i * 5) % 30;
                canvas_draw_disc(canvas, x, y, 2); // Black disc for Flip IQ preview
            }
        } else { // Space Flight
            int x = SCREEN_WIDTH / 2 + 12 + (furi_get_tick() / 100) % 20;
            int y = y_offset + 19 + (furi_get_tick() / 150) % 10;
            canvas_draw_str(canvas, x, y, "C>");
            canvas_draw_circle(canvas, x + 10, y + 12, 5);
        }
    }
}

// Draw loading screen
static void draw_loading_screen(Canvas* canvas) {
    if(!canvas) return;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, "Nah to the Nah Nah Nah", 10, SCREEN_HEIGHT / 2, SCREEN_WIDTH - 20, FontPrimary);
}

// Draw pause screen
static void draw_pause_screen(Canvas* canvas) {
    if(!canvas) return;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_frame(canvas, PORTRAIT_WIDTH / 2 - 20, PORTRAIT_HEIGHT / 2 - 10, 40, 20);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, PORTRAIT_WIDTH / 2 - 18, PORTRAIT_HEIGHT / 2 - 8, 36, 16);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, PORTRAIT_WIDTH / 2 - 12, PORTRAIT_HEIGHT / 2 + 4, "Pause");
}

// Draw rotate screen with Flipper animation
static void draw_rotate_screen(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    uint32_t elapsed = furi_get_tick() - ctx->rotate_start_time;
    if(elapsed < 1000) {
        canvas_draw_frame(canvas, 20, 10, 88, 44);
        canvas_draw_str(canvas, 54, 54, "FLIPPER");
        canvas_draw_circle(canvas, 54, 50, 10);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_disc(canvas, 90, 50, 5); // Filled smaller circle
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 64, 32, ">");
    } else if(elapsed < 6000 && !ctx->rotate_skip) {
        int32_t t = ((elapsed - 1000) * FIXED_POINT_SCALE) / 5000;
        ctx->rotate_angle = (t * 90) / FIXED_POINT_SCALE;
        ctx->zoom_factor = FIXED_POINT_SCALE + (t * 2);
        int w = (88 * ctx->zoom_factor) / FIXED_POINT_SCALE;
        int h = (44 * ctx->zoom_factor) / FIXED_POINT_SCALE;
        int x = (SCREEN_WIDTH - w) / 2;
        int y = (SCREEN_HEIGHT - h) / 2;
        canvas_draw_frame(canvas, x, y, w, h);
        canvas_draw_str(canvas, x + w / 2, y + h + 4, "FLIPPER");
        canvas_draw_circle(canvas, x + w / 2, y + h + (10 * ctx->zoom_factor) / FIXED_POINT_SCALE, (10 * ctx->zoom_factor) / FIXED_POINT_SCALE);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_disc(canvas, x + w - (5 * ctx->zoom_factor) / FIXED_POINT_SCALE, y + h + (5 * ctx->zoom_factor) / FIXED_POINT_SCALE, (5 * ctx->zoom_factor) / FIXED_POINT_SCALE);
        canvas_set_color(canvas, ColorBlack);
        int arrow_x = x + w / 2;
        int arrow_y = y + h / 2;
        canvas_draw_str(canvas, arrow_x, arrow_y, (t < FIXED_POINT_SCALE / 2) ? ">" : "^");
    } else {
        view_port_set_orientation(ctx->view_port, ctx->is_left_handed ? ViewPortOrientationVerticalFlip : ViewPortOrientationVertical);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
        canvas_set_color(canvas, ColorBlack);
        draw_word_wrapped_text(canvas, " PLEASE  ROTATE  YOUR  SCREEN     >>>>> ", 5, 20, PORTRAIT_WIDTH - 10, FontPrimary);
    }
}

// Draw Zero Hero game with arrow symbols
static void draw_zero_hero(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, 26); // Larger box for score and streak
    canvas_set_color(canvas, ColorBlack);
    for(int i = 0; i < 5; i++) {
        canvas_draw_line(canvas, i * 12 + 2, 26, i * 12 + 2, PORTRAIT_HEIGHT - 4);
        canvas_draw_str(canvas, i * 12 + 4, PORTRAIT_HEIGHT - 5, i == 0 ? "^" : i == 1 ? "<" : i == 2 ? "O" : i == 3 ? ">" : "v");
    }
    for(int i = 0; i < 5; i++) {
        canvas_set_color(canvas, ctx->strum_hit[i] ? ColorWhite : ColorBlack);
        canvas_draw_box(canvas, i * 12 + 2, PORTRAIT_HEIGHT - 6, 10, 2);
        if(ctx->strum_hit[i]) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_frame(canvas, i * 12 + 1, PORTRAIT_HEIGHT - 7, 12, 4);
            canvas_set_color(canvas, ColorWhite);
        }
    }
    canvas_draw_box(canvas, 0, PORTRAIT_HEIGHT - 4, PORTRAIT_WIDTH, 4);
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < 10; j++) {
            if(ctx->key_positions[i][j] > 0) {
                canvas_draw_str(canvas, i * 12 + 4, ctx->key_positions[i][j], i == 0 ? "^" : i == 1 ? "<" : i == 2 ? "O" : i == 3 ? ">" : "v");
            }
        }
    }
    char streak_str[32];
    snprintf(streak_str, sizeof(streak_str), "Streak: %d.%d", ctx->streak, ctx->oflow);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, streak_str, (PORTRAIT_WIDTH - strlen(streak_str) * 6) / 2, 17, PORTRAIT_WIDTH, FontSecondary);
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %d.%d", ctx->score, ctx->score_oflow);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, score_str, (PORTRAIT_WIDTH - strlen(score_str) * 6) / 2, 26, PORTRAIT_WIDTH, FontSecondary);
    if(ctx->is_day) {
        canvas_draw_circle(canvas, 2, 10, 3);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_circle(canvas, 2, 10, 3);
        canvas_set_color(canvas, ColorBlack);
    }
    draw_notification(canvas, ctx);
}

// Draw Flip Zip game with speed bar
static void draw_flip_zip(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, 26); // Larger box for score and streak
    canvas_set_color(canvas, ColorWhite);
    for(int i = 0; i < 5; i++) {
        canvas_draw_line(canvas, i * 12 + 2, 26, i * 12 + 2, PORTRAIT_HEIGHT - 5);
    }
    canvas_draw_line(canvas, 0, PORTRAIT_HEIGHT - 5, PORTRAIT_WIDTH, PORTRAIT_HEIGHT - 5);
    canvas_draw_line(canvas, 0, PORTRAIT_HEIGHT - 4, PORTRAIT_WIDTH * 4 / 5, PORTRAIT_HEIGHT - 4);
    canvas_draw_line(canvas, 0, PORTRAIT_HEIGHT - 3, PORTRAIT_WIDTH * 3 / 5, PORTRAIT_HEIGHT - 3);
    canvas_draw_line(canvas, 0, PORTRAIT_HEIGHT - 2, PORTRAIT_WIDTH * 2 / 5, PORTRAIT_HEIGHT - 2);
    canvas_draw_line(canvas, 0, PORTRAIT_HEIGHT - 1, PORTRAIT_WIDTH * 1 / 5, PORTRAIT_HEIGHT - 1);
    canvas_set_color(canvas, ColorBlack);
    const char* mascot_char = ctx->jump_scale > 0 ? "F" : "f"; 
    int mascot_y = PORTRAIT_HEIGHT - 7 - ctx->mascot_y - (ctx->is_jumping ? (ctx->jump_progress * 10 / FIXED_POINT_SCALE) : 0);
    canvas_draw_str(canvas, ctx->mascot_lane * 12 + 4, mascot_y, mascot_char);
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < 10; j++) {
            if(ctx->obstacle_positions[i][j] > 0) {
                char symbol = ctx->obstacles[i][j] == 1 ? 'O' : ctx->obstacles[i][j] == 2 ? '-' : 'S';
                canvas_draw_str(canvas, i * 12 + 4, ctx->obstacle_positions[i][j], &symbol);
            }
        }
    }
    char streak_str[32];
    snprintf(streak_str, sizeof(streak_str), "Streak: %d.%d", ctx->streak, ctx->oflow);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, streak_str, (PORTRAIT_WIDTH - strlen(streak_str) * 6) / 2, 17, PORTRAIT_WIDTH, FontSecondary);
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %d.%d", ctx->score, ctx->score_oflow);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, score_str, (PORTRAIT_WIDTH - strlen(score_str) * 6) / 2, 26, PORTRAIT_WIDTH, FontSecondary);
    if(ctx->is_day) {
        canvas_draw_circle(canvas, 2, 10, 3);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_circle(canvas, 2, 10, 3);
        canvas_set_color(canvas, ColorBlack);
    }
    // Draw speed bar
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, SPEED_BAR_X, SPEED_BAR_Y, SPEED_BAR_WIDTH, SPEED_BAR_HEIGHT);
    int reward_bpm_x = SPEED_BAR_X + (SPEED_BAR_WIDTH * 2 / 3); // 2/3 mark for reward BPM
    canvas_draw_line(canvas, reward_bpm_x, SPEED_BAR_Y - 2, reward_bpm_x, SPEED_BAR_Y + SPEED_BAR_HEIGHT + 1); // Reward BPM marker
    int speed_bpm = ctx->speed_bpm < MIN_SPEED_BPM ? MIN_SPEED_BPM : ctx->speed_bpm;
    int speed_bar_pos = SPEED_BAR_X + ((speed_bpm - MIN_SPEED_BPM) * SPEED_BAR_WIDTH) / (ctx->speed_bpm - MIN_SPEED_BPM + 1); // Scale BPM to bar width
    canvas_draw_line(canvas, speed_bar_pos, SPEED_BAR_Y, speed_bar_pos, SPEED_BAR_Y + SPEED_BAR_HEIGHT - 1);
    draw_notification(canvas, ctx);
}

// Draw Line Car game with scrolling tracks
static void draw_line_car(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, 26); // UI box for score and streak
    canvas_set_color(canvas, ColorWhite);
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
            if(ctx->track_positions[i][j] > 0 && ctx->track_positions[i][j] < PORTRAIT_HEIGHT) {
                canvas_draw_box(canvas, i * 12, ctx->track_positions[i][j] - ctx->track_pieces[i][j], 12, ctx->track_pieces[i][j]);
            }
        }
    }
    // Draw car: 3x1 base, 3-pixel line, 3x1 top with border
    int car_y = ctx->car_y;
    canvas_draw_box(canvas, ctx->car_lane * 12 + 4, car_y, 3, 1); // Base
    canvas_draw_line(canvas, ctx->car_lane * 12 + 5, car_y - 3, ctx->car_lane * 12 + 5, car_y); // Shaft
    canvas_draw_box(canvas, ctx->car_lane * 12 + 4, car_y - 4, 3, 1); // Top
    canvas_draw_frame(canvas, ctx->car_lane * 12 + 3, car_y - 5, 5, 6); // 1-pixel border with thick corners
    if(ctx->car_angle != 0) {
        int offset_x = (ctx->car_angle > 0) ? 2 : -2; // Offset for drift visualization
        canvas_draw_box(canvas, ctx->car_lane * 12 + 4 + offset_x, car_y, 3, 1);
    }
    // Wiggle during drift (medium/hard difficulty)
    if(ctx->is_drifting && ctx->difficulty > DIFFICULTY_EASY && abs(ctx->car_lane - ctx->prev_car_lane) > 2) {
        int dx = (rand() % 7) - 3; // -3 to +3 pixels
        canvas_draw_box(canvas, ctx->car_lane * 12 + 4 + dx, car_y, 3, 1);
        furi_delay_ms(3);
        canvas_draw_box(canvas, ctx->car_lane * 12 + 4, car_y, 3, 1);
    }
    char streak_str[32];
    snprintf(streak_str, sizeof(streak_str), "Streak: %d.%d", ctx->streak, ctx->oflow); // Drift multiplier
    canvas_set_color(canvas, ColorBlack);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, streak_str, (PORTRAIT_WIDTH - strlen(streak_str) * 6) / 2, 17, PORTRAIT_WIDTH, FontSecondary);
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %d.%d", ctx->score, ctx->score_oflow);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_color(canvas, ColorWhite);
    draw_word_wrapped_text(canvas, score_str, (PORTRAIT_WIDTH - strlen(score_str) * 6) / 2, 26, PORTRAIT_WIDTH, FontSecondary);
    if(ctx->is_day) {
        canvas_draw_circle(canvas, 2, 10, 3);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_circle(canvas, 2, 10, 3);
        canvas_set_color(canvas, ColorBlack);
    }
    draw_notification(canvas, ctx);
}

// Draw Line Car title screen
static void draw_line_car_title(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    uint32_t elapsed = furi_get_tick() - ctx->game_start_time;
    if(elapsed < 1300) {
        return; // Wait 1.3s for text to appear
    }
    draw_word_wrapped_text(canvas, "Line Car", PORTRAIT_WIDTH / 2 - 24, PORTRAIT_HEIGHT / 2 - 10, 48, FontPrimary);
    // Add bold border with white pixels
    for(int x = PORTRAIT_WIDTH / 2 - 28; x <= PORTRAIT_WIDTH / 2 + 20; x++) {
        for(int y = PORTRAIT_HEIGHT / 2 - 14; y <= PORTRAIT_HEIGHT / 2 + 2; y++) {
            canvas_draw_dot(canvas, x, y);
        }
    }
    draw_word_wrapped_text(canvas, "OK->PLAY", PORTRAIT_WIDTH / 2 - 20, PORTRAIT_HEIGHT - 10, 40, FontSecondary);
}

// Draw Flip IQ title screen
static void draw_flip_iq_title(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    uint32_t elapsed = furi_get_tick() - ctx->game_start_time;
    if(elapsed < 1300) {
        return; // Wait 1.3s for text to appear
    }
    draw_word_wrapped_text(canvas, "Flip IQ", PORTRAIT_WIDTH / 2 - 20, PORTRAIT_HEIGHT / 2 - 10, 40, FontPrimary);
    draw_word_wrapped_text(canvas, "OK->PLAY", PORTRAIT_WIDTH / 2 - 20, PORTRAIT_HEIGHT - 10, 40, FontSecondary);
}

// Draw Space Flight title screen
static void draw_space_flight_title(Canvas* canvas, GameContext* ctx) {
    if(!canvas || !ctx) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
    canvas_set_color(canvas, ColorBlack);
    uint32_t elapsed = furi_get_tick() - ctx->game_start_time;
    if(elapsed < 1300) {
        return; // Wait 1.3s for text to appear
    }
    draw_word_wrapped_text(canvas, "Space Flight", PORTRAIT_WIDTH / 2 - 24, PORTRAIT_HEIGHT / 2 - 10, 48, FontPrimary);
    draw_word_wrapped_text(canvas, "OK->PLAY", PORTRAIT_WIDTH / 2 - 20, PORTRAIT_HEIGHT - 10, 40, FontSecondary);
}

// Update Zero Hero game (AI-driven strumming)
static void update_zero_hero(GameContext* ctx) {
    if(!ctx) return;
    int fps = FPS_BASE + ctx->difficulty * 5;
    if(furi_get_tick() - ctx->last_ai_update < (uint32_t)(1000 / fps)) return;
    ctx->last_ai_update = furi_get_tick();
    for(int i = 0; i < 5; i++) {
        ctx->strum_hit[i] = false;
        for(int j = 0; j < 10; j++) {
            if(ctx->key_positions[i][j] > 0) {
                ctx->key_positions[i][j] += 1;
                if(ctx->key_positions[i][j] >= PORTRAIT_HEIGHT - 6 && ctx->key_positions[i][j] <= PORTRAIT_HEIGHT - 4) {
                    if(ctx->is_holding[i]) {
                        ctx->streak++;
                        ctx->score++;
                        ctx->key_positions[i][j] = 0;
                        ctx->strum_hit[i] = true;
                        if(ctx->streak >= MAX_STREAK_INT) {
                            ctx->streak = 0;
                            ctx->oflow++;
                        }
                        if(ctx->streak == 5) {
                            strcpy(ctx->notification_text, "! Perfect !");
                            ctx->last_notification_time = furi_get_tick();
                            ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                        } else if(ctx->streak == 6) {
                            strcpy(ctx->notification_text, "! STREAK STARTED !");
                            ctx->last_notification_time = furi_get_tick();
                            ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                        }
                        ctx->streak_sum += ctx->streak;
                        ctx->streak_count++;
                        if(ctx->streak > ctx->highest_streak) ctx->highest_streak = ctx->streak;
                    }
                } else if(ctx->key_positions[i][j] > PORTRAIT_HEIGHT - 5) {
                    ctx->key_positions[i][j] = 0;
                    ctx->streak = 0;
                    strcpy(ctx->notification_text, "! Miss !");
                    ctx->last_notification_time = furi_get_tick();
                    ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                }
            }
        }
    }
    if(ctx->ai_beat_counter++ % 10 == 0) {
        int lane = rand() % 5;
        for(int j = 0; j < 10; j++) {
            if(ctx->key_positions[lane][j] == 0) {
                ctx->key_positions[lane][j] = 7;
                break;
            }
        }
    }
    if(furi_get_tick() - ctx->last_difficulty_check > COOLDOWN_MS && ctx->streak > 5) {
        int avg_streak = ctx->streak_count > 0 ? ctx->streak_sum / ctx->streak_count : 0;
        if(ctx->streak >= avg_streak * 3) {
            if(ctx->difficulty < DIFFICULTY_HARD) ctx->difficulty++;
            ctx->last_difficulty_check = furi_get_tick();
            int msg_idx = rand() % (sizeof(notification_messages) / sizeof(notification_messages[0]));
            strcpy(ctx->notification_text, notification_messages[msg_idx]);
            ctx->last_notification_time = furi_get_tick();
            ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
        }
    }
}

// Update Flip Zip game (AI-driven speed, improved jump, tap DRM, and speed boost)
static void update_flip_zip(GameContext* ctx) {
    if(!ctx) return;
    int fps = FPS_BASE + (ctx->speed_bpm > 0 ? ctx->speed_bpm / 10 : 0);
    if(furi_get_tick() - ctx->last_ai_update < (uint32_t)(1000 / fps)) return;
    ctx->last_ai_update = furi_get_tick();
    int speed_modifier = 1 + ctx->speed_bpm / 60;
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < 10; j++) {
            if(ctx->obstacle_positions[i][j] > 0) {
                ctx->obstacle_positions[i][j] += speed_modifier;
                if(ctx->obstacle_positions[i][j] > PORTRAIT_HEIGHT - 7) {
                    ctx->obstacle_positions[i][j] = 0;
                    ctx->obstacles[i][j] = 0;
                    ctx->score++;
                    if(i == ctx->mascot_lane - 1 || i == ctx->mascot_lane + 1) {
                        ctx->successful_jumps++;
                        if(ctx->successful_jumps % 5 == 0) {
                            ctx->speed_bpm += 10;
                            if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                        }
                    }
                }
            }
        }
    }
    if(ctx->ai_beat_counter++ % 15 == 0) {
        int lane = rand() % 5;
        int type = rand() % 3 + 1;
        for(int j = 0; j < 10; j++) {
            if(ctx->obstacle_positions[lane][j] == 0) {
                ctx->obstacle_positions[lane][j] = 7;
                ctx->obstacles[lane][j] = type;
                break;
            }
        }
    }
    if(ctx->is_jumping) {
        ctx->jump_progress += 100;
        if(ctx->jump_progress < FIXED_POINT_SCALE / 2) {
            ctx->jump_scale = ctx->jump_progress / (FIXED_POINT_SCALE / 4);
        } else if(ctx->jump_progress < FIXED_POINT_SCALE) {
            ctx->jump_scale = (FIXED_POINT_SCALE - ctx->jump_progress) / (FIXED_POINT_SCALE / 4);
        } else {
            if(ctx->jump_hold_time > 0 && furi_get_tick() - ctx->jump_hold_time < (uint32_t)(ctx->speed_bpm * 250)) {
                ctx->jump_progress = FIXED_POINT_SCALE / 2;
                ctx->jump_scale = 1;
            } else {
                ctx->is_jumping = false;
                ctx->jump_progress = 0;
                ctx->jump_scale = 0;
                ctx->successful_jumps++;
                ctx->mascot_y += ctx->jump_y_accumulated; // Apply accumulated Up presses
                if(ctx->mascot_y > 20) ctx->mascot_y = 20; // Cap max height
                ctx->jump_y_accumulated = 0; // Reset after landing
                if(ctx->successful_jumps % 5 == 0) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
            }
        }
        // Move forward 1 pixel per 10ms while airborne
        if(ctx->jump_scale > 0) {
            uint32_t airborne_time = furi_get_tick() - ctx->jump_hold_time;
            ctx->mascot_y += airborne_time / 10;
            if(ctx->mascot_y > 20) ctx->mascot_y = 20; // Cap max height
        }
    }
}

// Update Line Car game (track scrolling, player movement, scoring)
static void update_line_car(GameContext* ctx) {
    if(!ctx) return;
    int fps = FPS_BASE + (ctx->speed_bpm > 0 ? ctx->speed_bpm / 10 : 0);
    if(furi_get_tick() - ctx->last_ai_update < (uint32_t)(1000 / fps)) return;
    ctx->last_ai_update = furi_get_tick();
    int speed_modifier = ctx->speed_bpm / 78; // Base speed at 78 BPM
    // Adjust speed based on player position
    if(ctx->car_y < ctx->fast_line) {
        if(furi_get_tick() - ctx->last_ai_update > 150) {
            ctx->speed_bpm += (ctx->speed_bpm * 0.01 < 700) ? 1 : 0; // Max 700% increase
            ctx->last_ai_update = furi_get_tick();
        }
    } else if(ctx->car_y > ctx->slow_line) {
        if(furi_get_tick() - ctx->last_ai_update > 199) {
            ctx->speed_bpm -= (ctx->speed_bpm * 0.01 > 66) ? 1 : 0; // Min 66% decrease
            ctx->last_ai_update = furi_get_tick();
        }
    }
    // Scroll tracks downward
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
            if(ctx->track_positions[i][j] > 0) {
                ctx->track_positions[i][j] += speed_modifier;
                if(ctx->track_positions[i][j] > PORTRAIT_HEIGHT) {
                    ctx->track_positions[i][j] = 0;
                    int length = (rand() % 37) + 9; // 9-45 pixels
                    ctx->track_pieces[i][j] = length;
                    ctx->track_positions[i][j] = -length; // Reset off-screen
                    // Randomly decide next lane direction
                    int next_lane = i + (rand() % 2 ? 1 : -1);
                    if(next_lane < 0) next_lane = 1; // Avoid edge wrap to left
                    if(next_lane > 4) next_lane = 3; // Avoid edge wrap to right
                    if(i == 4 && rand() % 2) next_lane = 4; // Allow straight tracks in last lane
                    ctx->track_positions[next_lane][j] = ctx->track_positions[i][j] - length;
                    ctx->track_pieces[next_lane][j] = length;
                }
            }
        }
    }
    // Check drift and scoring
    if(ctx->is_drifting && furi_get_tick() - ctx->last_drift_time > 693) {
        ctx->is_drifting = false;
        ctx->car_angle = 0;
        if(ctx->track_positions[ctx->car_lane][0] > 0 && ctx->car_y >= ctx->track_positions[ctx->car_lane][0] - ctx->track_pieces[ctx->car_lane][0]) {
            ctx->score += ctx->uber_points * ctx->drift_multiplier;
            snprintf(ctx->notification_text, sizeof(ctx->notification_text), line_car_notifications[0], ctx->uber_points * ctx->drift_multiplier);
        } else {
            snprintf(ctx->notification_text, sizeof(ctx->notification_text), line_car_notifications[3], ctx->uber_points * ctx->drift_multiplier);
        }
        ctx->last_notification_time = furi_get_tick();
        ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
        ctx->uber_points = 0;
        ctx->drift_multiplier = 1; // Reset multiplier
    }
    // Apply gravity if not holding Up
    if(!ctx->is_holding[0]) {
        ctx->car_y += speed_modifier;
        if(ctx->car_y > PORTRAIT_HEIGHT - 7) {
            ctx->car_y = PORTRAIT_HEIGHT - 7;
            for(int i = 0; i < 5; i++) {
                if(ctx->track_positions[i][0] > 0 && ctx->car_y >= ctx->track_positions[i][0] - ctx->track_pieces[i][0]) {
                    ctx->car_lane = i;
                    break;
                }
            }
            // Check for off-track
            if(ctx->track_positions[ctx->car_lane][0] == 0) {
                strcpy(ctx->notification_text, line_car_notifications[2]);
                ctx->last_notification_time = furi_get_tick();
                ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                // Reposition to nearest track
                for(int i = 0; i < 5; i++) {
                    for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
                        if(ctx->track_positions[i][j] > 0) {
                            ctx->car_y = ctx->track_positions[i][j] - ctx->track_pieces[i][j] + (rand() % 10);
                            ctx->car_lane = i;
                            break;
                        }
                    }
                    if(ctx->car_y < PORTRAIT_HEIGHT - 7) break;
                }
            }
        }
    }
    // Handle drift slowdown
    if(ctx->is_drifting && (ctx->car_y < ctx->fast_line || ctx->car_y > ctx->slow_line)) {
        ctx->speed_bpm -= (ctx->speed_bpm * 0.01 > 66) ? 1 : 0; // Slow during drift
    }
}

// <!-- SPLIT POINT FOR PART 2 -->

#if USE_SAM_TTS
// SAM Text-to-Speech function
static void SAMT2S(const char* text) {
    if(furi_hal_speaker_is_mine() || furi_hal_speaker_acquire(1000)) {
        char upper_text[32];
        strncpy(upper_text, text, sizeof(upper_text) - 1);
        upper_text[sizeof(upper_text) - 1] = '\0';
        for(size_t i = 0; upper_text[i] != '\0'; i++) {
            if(upper_text[i] >= 'a' && upper_text[i] <= 'z') {
                upper_text[i] = upper_text[i] - 'a' + 'A';
            }
        }
        sam_say(&voice, upper_text);
        furi_hal_speaker_release();
    }
}
#endif // USE_SAM_TTS

// Update Flip IQ game
static void update_flip_iq(GameContext* ctx) {
    if(!ctx) return;
    int fps = FPS_BASE + (ctx->speed_bpm > 0 ? ctx->speed_bpm / 10 : 0);
    if(furi_get_tick() - ctx->last_ai_update < (uint32_t)(1000 / fps)) return;
    ctx->last_ai_update = furi_get_tick();
    int speed_modifier = ctx->speed_bpm / 78; // Base speed at 78 BPM
    // Adjust speed based on streak and misses
    if(ctx->streak > 0 && furi_get_tick() - ctx->last_ai_update > 150) {
        ctx->speed_bpm += (ctx->speed_bpm * 0.01 < 700) ? 1 : 0; // Max 700% increase
    } else if(ctx->streak == 0 && furi_get_tick() - ctx->last_ai_update > 199) {
        ctx->speed_bpm -= (ctx->speed_bpm * 0.01 > 66) ? 1 : 0; // Min 66% decrease
    }

    // Handle initial ball drop and round start
    if(ctx->game_start_time == 0) {
        ctx->game_start_time = furi_get_tick(); // Start timer
        ctx->round_start_time = furi_get_tick();
        ctx->ball_width = (rand() % (ctx->streak > 10 ? 10 : ctx->streak) + 10); // 10-20 pixels
        ctx->key_columns[2][0] = ctx->ball_width;
        ctx->key_positions[2][0] = 26; // Start at background top
        ctx->active_lanes = 5; // Start with all lanes
        ctx->ball_count = ctx->ball_width * 6; // Max balls based on width
        int miss_percent = rand() % 21; // 0-20% missed balls
        ctx->ball_count -= (ctx->ball_count * miss_percent) / 100;
        ctx->ball_count = ctx->ball_count > WORLD_OBJ_LIMIT ? WORLD_OBJ_LIMIT : ctx->ball_count; // Cap at global limit
        // Comment: Adjust WORLD_OBJ_LIMIT or miss_percent for performance/difficulty tuning
    }

    uint32_t elapsed = (furi_get_tick() - ctx->round_start_time) / 1000;
    uint32_t round_time = 30 + (ctx->streak - 1) * 30; // 30s + 30s per round
    if(elapsed > round_time - 9 && ctx->key_positions[2][0] == 0) {
        ctx->score += 10; // Round end bonus
        snprintf(ctx->notification_text, sizeof(ctx->notification_text), "Round End. +10 PP");
        ctx->last_notification_time = furi_get_tick();
        ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
        ctx->streak++; // Increment streak
        if(ctx->streak > 99) ctx->streak = 1; // Loop back to 1
        ctx->round_start_time = furi_get_tick(); // Reset for next round
        ctx->ball_width = (rand() % (ctx->streak > 10 ? 10 : ctx->streak) + 10); // New ball width
        ctx->key_columns[2][0] = ctx->ball_width;
        ctx->key_positions[2][0] = 26;
        ctx->ball_count = ctx->ball_width * 6 * (100 - (rand() % 21)) / 100; // Recalculate with miss percent
        ctx->ball_count = ctx->ball_count > WORLD_OBJ_LIMIT ? WORLD_OBJ_LIMIT : ctx->ball_count;
        // Comment: Adjust ball_count or miss_percent for difficulty tuning
    }

    // Move and spawn balls
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
            if(ctx->key_positions[i][j] > 0) {
                ctx->key_positions[i][j] += speed_modifier;
                if(ctx->key_positions[i][j] > 46 && ctx->key_positions[i][j] < 46 + 20 && rand() % 4 == 0) {
                    ctx->ball_broken[j] = true; // 25% break chance
                }
                if(ctx->key_positions[i][j] > PORTRAIT_HEIGHT - 7) {
                    ctx->key_positions[i][j] = 0;
                    ctx->key_columns[i][j] = 0;
                    if(i == ctx->car_lane && ctx->car_y + 3 >= ctx->key_positions[i][j] - ctx->key_columns[i][j]) {
                        if(ctx->ball_broken[j] && ctx->is_holding[0]) {
                            ctx->car_y -= ctx->key_columns[i][j]; // Climb over
                            ctx->score += 1; // Add to hidden PP score
                            int msg_idx = rand() % (sizeof(flip_iq_notifications_positive) / sizeof(flip_iq_notifications_positive[0]));
                            strcpy(ctx->notification_text, flip_iq_notifications_positive[msg_idx]);
                            ctx->last_notification_time = furi_get_tick();
                            ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                        } else {
                            ctx->streak = 0; // Stumble
                            int msg_idx = rand() % (sizeof(flip_iq_notifications_negative) / sizeof(flip_iq_notifications_negative[0]));
                            strcpy(ctx->notification_text, flip_iq_notifications_negative[msg_idx]);
                            ctx->last_notification_time = furi_get_tick();
                            ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                        }
                    }
                }
            }
        }
    }
    // Comment: Adjust spawn rate or lane change frequency for difficulty
    if(ctx->ai_beat_counter++ % 15 == 0 && ctx->ball_count > 0) {
        int lane = rand() % ctx->active_lanes;
        for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
            if(ctx->key_positions[lane][j] == 0) {
                ctx->key_positions[lane][j] = 46; // Start at game board top
                ctx->key_columns[lane][j] = ctx->ball_width;
                ctx->ball_count--;
                break;
            }
        }
    }
}

// Update Tectone Sim game
static void update_tectone_sim(GameContext* ctx) {
    if(!ctx) return;
    int fps = FPS_BASE + (ctx->speed_bpm > 0 ? ctx->speed_bpm / 10 : 0);
    if(furi_get_tick() - ctx->last_ai_update < (uint32_t)(1000 / fps)) return;
    ctx->last_ai_update = furi_get_tick();
    int speed_modifier = ctx->speed_bpm / 58; // Base speed at 58 BPM
    // Comment: Adjust base BPM (58) for comment scroll speed tuning

    // Handle emotion updates
    if(furi_get_tick() - ctx->emotion_cooldown > 1000) {
        if(ctx->is_holding[1] && ctx->anger < 9) { // Left: Anger
            ctx->anger++;
            ctx->emotion_cooldown = furi_get_tick();
        } else if(ctx->is_holding[0] && ctx->based < 9) { // Up: Based
            ctx->based++;
            ctx->emotion_cooldown = furi_get_tick();
        } else if(ctx->is_holding[3] && ctx->cuteness < 9) { // Right: Cuteness
            ctx->cuteness++;
            ctx->emotion_cooldown = furi_get_tick();
        } else if(ctx->is_holding[4]) { // Down: Prop
            ctx->emotion_cooldown = furi_get_tick();
            int prop = rand() % 3; // 0: Microphone, 1: Shotgun, 2: Ball
            if(prop == 0) { // Microphone
                ctx->anger += rand() % 2 ? 1 : -1;
                if(ctx->anger < 0) ctx->anger = 0;
                if(ctx->anger > 9) ctx->anger = 9;
            } else if(prop == 1) { // Shotgun
                ctx->based += rand() % 2 ? 1 : -1;
                if(ctx->based < 0) ctx->based = 0;
                if(ctx->based > 9) ctx->based = 9;
                furi_hal_vibro_on(true);
                furi_delay_ms(32);
                furi_hal_vibro_on(false);
            } else { // Ball
                ctx->cuteness += rand() % 2 ? 1 : -1;
                if(ctx->cuteness < 0) ctx->cuteness = 0;
                if(ctx->cuteness > 9) ctx->cuteness = 9;
            }
        } else if(ctx->is_holding[2]) { // OK: Random emotion
            int emotion = rand() % 4;
            if(emotion == 0) ctx->anger += (ctx->anger < 9) ? 1 : 0;
            else if(emotion == 1) ctx->based += (ctx->based < 9) ? 1 : 0;
            else if(emotion == 2) ctx->cuteness += (ctx->cuteness < 9) ? 1 : 0;
            else ctx->sad += (ctx->sad < 9) ? 1 : 0;
            ctx->emotion_cooldown = furi_get_tick();
        }
    }

    // Emotion thresholds and actions
    char phrase_buffer[32]; // Buffer to store selected phrase
    if(ctx->anger == 0) {
        ctx->cuteness = 3; // Reset cuteness
        ctx->anger = 5; // Reset anger
        int idx = rand() % 4;
        strncpy(phrase_buffer, tectone_emotion_phrases[1][idx], sizeof(phrase_buffer) - 1); // Cuteness phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
    } else if(ctx->anger == 9) {
        ctx->cuteness = 3; // Reset cuteness
        ctx->anger = 5; // Reset anger
        int idx = rand() % 4;
        strncpy(phrase_buffer, tectone_emotion_phrases[2][idx], sizeof(phrase_buffer) - 1); // Anger phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
        if(idx == 0) { // Slam desk
            int slams = rand() % 15 + 1;
            for(int i = 0; i < slams; i++) {
                furi_hal_vibro_on(true);
                furi_delay_ms(32);
                furi_hal_vibro_on(false);
                furi_delay_ms(50);
            }
        }
    }
    if(ctx->based == 0) {
        ctx->sad = 4; // Reset sad
        ctx->based = 7; // Reset based
        int idx = rand() % 4;
        strncpy(phrase_buffer, tectone_emotion_phrases[3][idx], sizeof(phrase_buffer) - 1); // Sad phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
    } else if(ctx->based == 9) {
        ctx->sad = 4; // Reset sad
        ctx->based = 7; // Reset based
        int idx = rand() % 4;
        strncpy(phrase_buffer, tectone_emotion_phrases[0][idx], sizeof(phrase_buffer) - 1); // Based phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
        if(idx == 1) { // Pump gun
            furi_hal_vibro_on(true);
            furi_delay_ms(700);
            furi_hal_vibro_on(false);
        }
    }
    if(ctx->cuteness == 0) {
        ctx->based++; // Increase based
        ctx->cuteness = 3; // Reset cuteness
        int idx = rand() % 2 ? 0 : 2;
        strncpy(phrase_buffer, tectone_emotion_phrases[idx][rand() % 4], sizeof(phrase_buffer) - 1); // Sad or anger phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
    } else if(ctx->cuteness == 9) {
        ctx->based++; // Increase based
        ctx->cuteness = 3; // Reset cuteness
        int idx = rand() % 2 ? 1 : rand() % 4; // Cuteness or random
        strncpy(phrase_buffer, tectone_emotion_phrases[1][idx], sizeof(phrase_buffer) - 1); // Cuteness phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
    }
    if(ctx->sad == 0) {
        ctx->anger++; // Increase anger
        ctx->sad = 4; // Reset sad
        int idx = rand() % 4;
        strncpy(phrase_buffer, tectone_emotion_phrases[0][idx], sizeof(phrase_buffer) - 1); // Based phrase
        phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
        #if USE_SAM_TTS
        SAMT2S(phrase_buffer);
        #endif
    } else if(ctx->sad == 9) {
        ctx->anger++; // Increase anger
        ctx->sad = 4; // Reset sad
        int idx = rand() % 5;
        if(idx == 3) { // Beep sounds
            strncpy(phrase_buffer, "Beep Beep", sizeof(phrase_buffer) - 1);
            phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
            #if USE_SAM_TTS
            SAMT2S(phrase_buffer);
            #endif
            furi_delay_ms(15000);
        } else if(idx == 0) { // Go to bed
            ctx->tectone_x = -10; // Off-screen
            furi_delay_ms(45000);
            ctx->is_day = false; // Lights off
            furi_delay_ms(30000);
            ctx->is_day = true; // Lights on
            ctx->tectone_x = PORTRAIT_WIDTH / 2 - 3;
        } else if(idx == 1) { // Exit screen
            ctx->tectone_x = -10;
            furi_delay_ms(8000);
            ctx->tectone_x = PORTRAIT_WIDTH / 2 - 3;
        } else if(idx == 2) { // Turn off lights
            ctx->is_day = false;
            furi_delay_ms(30000);
            ctx->is_day = true;
        } else { // Hide chat
            for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
                ctx->comment_positions[i] = 0;
                ctx->comment_heights[i] = 0;
            }
            furi_delay_ms(45000);
        }
    }

    // Move Tectone
    uint32_t base_move_cooldown = 500; // Base cooldown in ms
    if(ctx->based > 7 || ctx->sad > 7) base_move_cooldown -= 10; // Faster movement
    if(furi_get_tick() - ctx->last_move_time > base_move_cooldown) {
        ctx->tectone_x += (rand() % 2 ? 3 : -3); // Move 3 pixels
        if(ctx->tectone_x < 0) ctx->tectone_x = 0;
        if(ctx->tectone_x > PORTRAIT_WIDTH - 10) ctx->tectone_x = PORTRAIT_WIDTH - 10;
        ctx->last_move_time = furi_get_tick();
        // Comment: Adjust base_move_cooldown or movement range for Tectone's speed
    }

    // Handle comments
    static int last_comment_side = -1;
    static int same_side_count = 0;
    if(furi_get_tick() - ctx->last_move_time > 1000 && !ctx->hype_cooldown) {
        int side = rand() % 2; // 0: Twitch (left), 1: YouTube (right)
        if(last_comment_side == side) same_side_count++;
        else same_side_count = 0;
        last_comment_side = side;
        if(same_side_count >= 3 || (rand() % 4 == 3)) { // Hype train trigger
            ctx->hype_train[0] = true;
            ctx->hype_cooldown = furi_get_tick() + 15000; // 15s cooldown
            strncpy(phrase_buffer, "HYPE TRAIN", sizeof(phrase_buffer) - 1);
            phrase_buffer[sizeof(phrase_buffer) - 1] = '\0';
            #if USE_SAM_TTS
            SAMT2S(phrase_buffer);
            #endif
            // Comment: Adjust hype_cooldown or same_side_count threshold for hype train frequency
        }
        for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
            if(ctx->comment_positions[i] > 0) {
                ctx->comment_positions[i] -= speed_modifier; // Scroll comments upward
                if(ctx->comment_positions[i] < 0) {
                    ctx->comment_positions[i] = 0;
                    ctx->comment_heights[i] = 0;
                }
            } else if(rand() % 100 < 10) { // 10% spawn chance
                ctx->comment_heights[i] = 10; // Fixed height for comments
                ctx->comment_positions[i] = 47; // Start at bedroom top
                // Comment: Adjust spawn chance or comment height for visibility
                break;
            }
        }
    }
}

static void update_space_flight(GameContext* ctx) {
    if(!ctx) return;
    int fps = FPS_BASE + (ctx->speed_bpm > 0 ? ctx->speed_bpm / 10 : 0);
    if(furi_get_tick() - ctx->last_ai_update < (uint32_t)(1000 / fps)) return;
    ctx->last_ai_update = furi_get_tick();
    int speed_modifier = ctx->speed_bpm / 78; // Base speed at 78 BPM
    // Comment: Adjust base BPM (78) for object scroll speed tuning

    // Update objects
    for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
        if(ctx->objects[i][2] > 0) { // size > 0
            ctx->objects[i][1] += speed_modifier; // Move downward by default
            if(ctx->screen_type == 1) ctx->objects[i][1] -= speed_modifier * 2; // Upward
            else if(ctx->screen_type == 2) ctx->objects[i][1] += speed_modifier * 2; // Downward
            else if(ctx->screen_type == 3) ctx->objects[i][0] -= speed_modifier; // Strife left
            else if(ctx->screen_type == 4) ctx->objects[i][0] += speed_modifier; // Strife right
            else if(ctx->screen_type == 5 || ctx->screen_type == 6) ctx->objects[i][1] += speed_modifier * 2; // Loop or barrel roll
            // Scale based on distance
            ctx->objects[i][2] += speed_modifier / 2;
            if(ctx->objects[i][2] > PORTRAIT_WIDTH / 3 && ctx->objects[i][2] < PORTRAIT_WIDTH / 2) {
                int damage = ctx->objects[i][2]; // Damage based on size
                if(ctx->objects[i][0] > 5 && ctx->objects[i][0] < PORTRAIT_WIDTH - 5 &&
                   ctx->objects[i][1] > 36 + 13 && ctx->objects[i][1] < 101 - 13) {
                    if(ctx->screen_type != 0) damage /= 2; // Half damage if moving
                    if(abs(ctx->objects[i][0] - PORTRAIT_WIDTH / 2) < 5) damage *= 2; // Double damage if centered
                    if(ctx->ship_armor > 0) ctx->ship_armor -= damage;
                    else ctx->ship_health -= damage;
                    if(ctx->ship_health <= 0) {
                        ctx->ship_health = (rand() % 191) + 9; // Reset health
                        ctx->ship_armor = (rand() % 81) + 19; // Reset armor
                        ctx->screen_type = 8; // Dock sequence
                        ctx->last_sequence_time = furi_get_tick();
                    }
                }
            }
            if(ctx->objects[i][1] > 101 || ctx->objects[i][1] < 36) ctx->objects[i][2] = 0; // Off-screen
        } else if(rand() % 100 < 10) { // 10% spawn chance
            ctx->objects[i][0] = rand() % 64; // Random x
            ctx->objects[i][1] = 36; // Start above HUD
            ctx->objects[i][2] = (rand() % 10) + 5; // 5-14 pixel size
            // Comment: Adjust spawn chance or object size range for difficulty
            if(ctx->ship_armor == 0 && rand() % 100 < 3) { // 3% health pickup
                ctx->objects[i][2] = -10; // Negative size for health pickup
            } else if(rand() % 100 < 25) { // 25% armor pickup
                ctx->objects[i][2] = -5; // Negative size for armor pickup
            }
        } else if(ctx->objects[i][2] < 0) { // Handle pickups
            if(ctx->objects[i][0] > 5 && ctx->objects[i][0] < PORTRAIT_WIDTH - 5 &&
               ctx->objects[i][1] > 36 + 13 && ctx->objects[i][1] < 101 - 13) {
                if(ctx->objects[i][2] == -10) ctx->ship_health += 10; // Health pickup
                else if(ctx->objects[i][2] == -5) ctx->ship_armor += 5; // Armor pickup
                ctx->objects[i][2] = 0; // Remove pickup
            }
        }
    }

    // Handle input sequences
    if(furi_get_tick() - ctx->last_sequence_time > 1963) { // 1963ms cooldown
        for(int i = 0; i < 4; i++) ctx->recent_inputs[i] = ctx->recent_inputs[i + 1];
        ctx->recent_inputs[4] = -1; // Placeholder
        if(ctx->is_holding[0]) ctx->recent_inputs[4] = 0; // Up
        else if(ctx->is_holding[4]) ctx->recent_inputs[4] = 4; // Down
        else if(ctx->is_holding[1]) ctx->recent_inputs[4] = 1; // Left
        else if(ctx->is_holding[3]) ctx->recent_inputs[4] = 3; // Right
        else if(ctx->is_holding[2]) ctx->recent_inputs[4] = 2; // OK
        // Check sequences
        if(ctx->recent_inputs[0] == 0 && ctx->recent_inputs[1] == 0 && ctx->recent_inputs[2] == 0 && ctx->recent_inputs[3] == 0 && ctx->recent_inputs[4] == 2) {
            ctx->screen_type = 5; // Loop up
            ctx->last_sequence_time = furi_get_tick();
            furi_hal_vibro_on(true);
            furi_delay_ms(32);
            furi_hal_vibro_on(false);
        } else if(ctx->recent_inputs[0] == 0 && ctx->recent_inputs[1] == 4 && ctx->recent_inputs[2] == 4 && ctx->recent_inputs[3] == 4 && ctx->recent_inputs[4] == 2) {
            ctx->screen_type = 5; // Loop down
            ctx->last_sequence_time = furi_get_tick();
            furi_hal_vibro_on(true);
            furi_delay_ms(32);
            furi_hal_vibro_on(false);
        } else if(ctx->recent_inputs[0] == 1 && ctx->recent_inputs[1] == 1 && ctx->recent_inputs[2] == 1 && ctx->recent_inputs[3] == 1 && ctx->recent_inputs[4] == 1) {
            ctx->screen_type = 6; // Barrel roll left
            ctx->last_sequence_time = furi_get_tick();
            furi_hal_vibro_on(true);
            furi_delay_ms(32);
            furi_hal_vibro_on(false);
        } else if(ctx->recent_inputs[0] == 3 && ctx->recent_inputs[1] == 3 && ctx->recent_inputs[2] == 3 && ctx->recent_inputs[3] == 3 && ctx->recent_inputs[4] == 3) {
            ctx->screen_type = 6; // Barrel roll right
            ctx->last_sequence_time = furi_get_tick();
            furi_hal_vibro_on(true);
            furi_delay_ms(32);
            furi_hal_vibro_on(false);
        }
    }
}

// Input callback for handling all game inputs
static void input_callback(InputEvent* input, void* ctx_ptr) {
    GameContext* ctx = ctx_ptr;
    if(!ctx) return;
    uint32_t now = furi_get_tick();
    if(now - ctx->last_input_time < TAP_DRM_MS) ctx->rapid_click_count++;
    else {
        ctx->rapid_click_count = 1;
        ctx->tap_count = 0;
        ctx->tap_window_start = now;
    }
    ctx->last_input_time = now;
    bool is_press = input->type == InputTypePress;
    bool is_release = input->type == InputTypeRelease;
    bool is_short = input->type == InputTypeShort;

    if(input->key == InputKeyBack && (now - ctx->last_back_press_time < BACK_BUTTON_COOLDOWN)) {
        return;
    }
    if(input->key == InputKeyBack && is_short) {
        ctx->last_back_press_time = now;
    }

    if(ctx->state == GAME_STATE_LOADING) {
        // No input during loading
    } else if(ctx->state == GAME_STATE_TITLE) {
        if(is_short && input->key == InputKeyLeft) {
            ctx->selected_side = 0;
        } else if(is_short && input->key == InputKeyRight) {
            ctx->selected_side = 1;
        } else if(is_short && input->key == InputKeyUp && ctx->selected_row > 0) {
            ctx->selected_row--;
        } else if(is_short && input->key == InputKeyDown && ctx->selected_row < 2) {
            ctx->selected_row++;
        } else if(is_short && input->key == InputKeyOk) {
            ctx->selected_game = ctx->selected_row * 2 + ctx->selected_side;
            ctx->state = GAME_STATE_ROTATE;
            ctx->rotate_start_time = now;
            ctx->rotate_angle = 0;
            ctx->zoom_factor = FIXED_POINT_SCALE;
            ctx->rotate_skip = false;
        } else if(is_short && input->key == InputKeyBack && ctx->note_q_a == 0) {
            ctx->start_back_count++;
            if(ctx->start_back_count >= 3) {
                ctx->state = GAME_STATE_CREDITS;
                ctx->credits_y = SCREEN_HEIGHT + 10 * ((int)(sizeof(credits_lines) / sizeof(credits_lines[0])) - 1);
                ctx->start_back_count = 0;
            }
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(is_release && input->key == InputKeyBack && ctx->back_hold_start > 0) {
            if(now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
                ctx->is_left_handed = !ctx->is_left_handed;
            }
            ctx->back_hold_start = 0;
        }
    } else if(ctx->state == GAME_STATE_ROTATE) {
        if(is_press) {
            ctx->rotate_skip = true;
            ctx->state = ctx->selected_game == GAME_MODE_ZERO_HERO ? GAME_STATE_ZERO_HERO :
                         ctx->selected_game == GAME_MODE_FLIP_ZIP ? GAME_STATE_FLIP_ZIP :
                         ctx->selected_game == GAME_MODE_LINE_CAR ? GAME_STATE_LINE_CAR :
                         ctx->selected_game == GAME_MODE_FLIP_IQ ? GAME_STATE_FLIP_IQ :
                         ctx->selected_game == GAME_MODE_TECTONE_SIM ? GAME_STATE_TECTONE_SIM :
                         ctx->selected_game == GAME_MODE_SPACE_FLIGHT ? GAME_STATE_SPACE_FLIGHT : GAME_STATE_ZERO_HERO;
            ctx->streak = 0; // Initialize streak to 0
            ctx->game_start_time = now;
            ctx->day_night_toggle_time = now + 300000;
            ctx->is_day = true;
            // Initialize game-specific states
            if(ctx->state == GAME_STATE_LINE_CAR) {
                ctx->car_lane = 2;
                ctx->car_y = PORTRAIT_HEIGHT - 7;
                ctx->car_angle = 0;
                ctx->uber_points = 0;
                ctx->drift_multiplier = 1;
                ctx->fast_line = 46; // 20 pixels below UI
                ctx->fast_line = 46; // 20 pixels below UI
                ctx->slow_line = 101; // 20 pixels above marquee
                ctx->prev_car_lane = ctx->car_lane;
                for(int i = 0; i < 5; i++) {
                    for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
                        ctx->track_positions[i][j] = 0;
                        int length = (rand() % 37) + 9; // 9-45 pixels
                        if(j < (rand() % 6) + 3) { // 3-8 initial pieces
                            ctx->track_pieces[i][j] = length;
                            ctx->track_positions[i][j] = PORTRAIT_HEIGHT - length + (rand() % (PORTRAIT_HEIGHT - length));
                        }
                    }
                }
            } else if(ctx->state == GAME_STATE_FLIP_IQ) {
                ctx->car_lane = 2; // Initial lane
                ctx->car_y = PORTRAIT_HEIGHT - 10; // Initial position
                for(int i = 0; i < 5; i++) {
                    for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
                        ctx->key_columns[i][j] = 0;
                        ctx->key_positions[i][j] = 0;
                    }
                }
            } else if(ctx->state == GAME_STATE_TECTONE_SIM) {
                ctx->anger = 5;
                ctx->based = 7;
                ctx->cuteness = 3;
                ctx->sad = 4;
                ctx->tectone_x = PORTRAIT_WIDTH / 2 - 3;
                ctx->move_cooldown = 500; // Base cooldown
                ctx->last_move_time = now;
                for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
                    ctx->comment_positions[i] = 0;
                    ctx->comment_heights[i] = 0;
                    ctx->hype_train[i] = false;
                }
            } else if(ctx->state == GAME_STATE_SPACE_FLIGHT) {
                ctx->ship_health = (rand() % 191) + 9; // 9-199
                ctx->ship_armor = (rand() % 81) + 19; // 19-99
                ctx->screen_type = 0; // Forward
                for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
                    ctx->objects[i][0] = 0;
                    ctx->objects[i][1] = 0;
                    ctx->objects[i][2] = 0;
                }
                for(int i = 0; i < 5; i++) ctx->recent_inputs[i] = -1;
            }
        }
    } else if(ctx->state == GAME_STATE_ZERO_HERO) {
        if(is_short && input->key == InputKeyBack) {
            ctx->state = GAME_STATE_PAUSE;
            ctx->pause_back_count = 0;
            ctx->back_hold_start = 0;
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(input->key == InputKeyBack && now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
            ctx->is_left_handed = !ctx->is_left_handed;
        } else {
            int key_idx = input->key == InputKeyUp ? 0 : input->key == InputKeyLeft ? 1 : input->key == InputKeyOk ? 2 : input->key == InputKeyRight ? 3 : input->key == InputKeyDown ? 4 : -1;
            if(key_idx >= 0) ctx->is_holding[key_idx] = is_press;
        }
    } else if(ctx->state == GAME_STATE_FLIP_ZIP) {
        if(is_short && input->key == InputKeyBack) {
            ctx->state = GAME_STATE_PAUSE;
            ctx->pause_back_count = 0;
            ctx->back_hold_start = 0;
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(input->key == InputKeyBack && now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
            ctx->is_left_handed = !ctx->is_left_handed;
        } else {
            if(is_short && input->key == InputKeyLeft && ctx->mascot_lane > 0) {
                ctx->mascot_lane--;
                ctx->tap_count++;
                if(now - ctx->tap_window_start >= 60000) {
                    ctx->tap_count = 1;
                    ctx->tap_window_start = now;
                }
                float tap_bpm = (ctx->tap_count * 60000.0f) / (float)(now - ctx->tap_window_start + 1);
                if(fabsf(tap_bpm - (float)ctx->speed_bpm) < 5.0f) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
            }
            if(is_short && input->key == InputKeyRight && ctx->mascot_lane < 4) {
                ctx->mascot_lane++;
                ctx->tap_count++;
                if(now - ctx->tap_window_start >= 60000) {
                    ctx->tap_count = 1;
                    ctx->tap_window_start = now;
                }
                float tap_bpm = (ctx->tap_count * 60000.0f) / (float)(now - ctx->tap_window_start + 1);
                if(fabsf(tap_bpm - (float)ctx->speed_bpm) < 5.0f) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
            }
            if(is_short && input->key == InputKeyUp && ctx->mascot_y < 20) {
                ctx->mascot_y++;
                if(ctx->is_jumping) {
                    ctx->jump_y_accumulated++;
                }
            }
            if(is_short && input->key == InputKeyDown && ctx->mascot_y > 0) {
                ctx->mascot_y--;
            }
            if(is_press && input->key == InputKeyOk && !ctx->is_jumping) {
                ctx->is_jumping = true;
                ctx->jump_progress = 0;
                ctx->jump_scale = 0;
                ctx->jump_hold_time = now;
                ctx->jump_y_accumulated = 0;
            } else if(is_release && input->key == InputKeyOk) {
                ctx->jump_hold_time = 0;
            }
        }
    } else if(ctx->state == GAME_STATE_LINE_CAR) {
        if(is_short && input->key == InputKeyBack) {
            ctx->state = GAME_STATE_PAUSE;
            ctx->pause_back_count = 0;
            ctx->back_hold_start = 0;
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(input->key == InputKeyBack && now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
            ctx->is_left_handed = !ctx->is_left_handed;
        } else {
            int key_idx = input->key == InputKeyUp ? 0 : input->key == InputKeyLeft ? 1 : input->key == InputKeyRight ? 3 : input->key == InputKeyDown ? 4 : -1;
            if(key_idx >= 0) ctx->is_holding[key_idx] = is_press;
            if(is_short && input->key == InputKeyLeft && ctx->car_lane > 0) {
                ctx->prev_car_lane = ctx->car_lane;
                ctx->car_lane--;
                ctx->tap_count++;
                if(now - ctx->tap_window_start >= 60000) {
                    ctx->tap_count = 1;
                    ctx->tap_window_start = now;
                }
                float tap_bpm = (ctx->tap_count * 60000.0f) / (float)(now - ctx->tap_window_start + 1);
                if(fabsf(tap_bpm - (float)ctx->speed_bpm) < 5.0f) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
                if(ctx->is_holding[4]) { // Drifting with Down
                    ctx->is_drifting = true;
                    ctx->car_angle = -8; // Drift angle
                    ctx->last_drift_time = now;
                    ctx->drift_multiplier++;
                } else {
                    ctx->car_angle = -15; // Rotation angle
                    if(ctx->track_positions[ctx->car_lane][0] > 0 && ctx->car_y >= ctx->track_positions[ctx->car_lane][0] - ctx->track_pieces[ctx->car_lane][0]) {
                        ctx->uber_points++;
                        snprintf(ctx->notification_text, sizeof(ctx->notification_text), line_car_notifications[0], ctx->uber_points);
                        ctx->last_notification_time = furi_get_tick();
                        ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                    }
                }
            }
            if(is_short && input->key == InputKeyRight && ctx->car_lane < 4) {
                ctx->prev_car_lane = ctx->car_lane;
                ctx->car_lane++;
                ctx->tap_count++;
                if(now - ctx->tap_window_start >= 60000) {
                    ctx->tap_count = 1;
                    ctx->tap_window_start = now;
                }
                float tap_bpm = (ctx->tap_count * 60000.0f) / (float)(now - ctx->tap_window_start + 1);
                if(fabsf(tap_bpm - (float)ctx->speed_bpm) < 5.0f) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
                if(ctx->is_holding[4]) { // Drifting with Down
                    ctx->is_drifting = true;
                    ctx->car_angle = 8; // Drift angle
                    ctx->last_drift_time = now;
                    ctx->drift_multiplier++;
                } else {
                    ctx->car_angle = 15; // Rotation angle
                    if(ctx->track_positions[ctx->car_lane][0] > 0 && ctx->car_y >= ctx->track_positions[ctx->car_lane][0] - ctx->track_pieces[ctx->car_lane][0]) {
                        ctx->uber_points++;
                        snprintf(ctx->notification_text, sizeof(ctx->notification_text), line_car_notifications[0], ctx->uber_points);
                        ctx->last_notification_time = furi_get_tick();
                        ctx->notification_x = (PORTRAIT_WIDTH - strlen(ctx->notification_text) * 6) / 2;
                    }
                }
            }
            if(is_press && input->key == InputKeyUp) {
                ctx->is_holding[0] = true;
            } else if(is_release && input->key == InputKeyUp) {
                ctx->is_holding[0] = false;
            }
            if(is_short && input->key == InputKeyDown) {
                if(ctx->is_drifting) {
                    // Handle wiggles (already in render)
                }
            }
            if(is_short && input->key == InputKeyBack) {
                ctx->speed_bpm -= (ctx->speed_bpm > MIN_SPEED_BPM) ? 1 : 0; // Brake slows speed
            }
        }
    } else if(ctx->state == GAME_STATE_FLIP_IQ) {
        if(is_short && input->key == InputKeyBack) {
            ctx->state = GAME_STATE_PAUSE;
            ctx->pause_back_count = 0;
            ctx->back_hold_start = 0;
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(input->key == InputKeyBack && now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
            ctx->is_left_handed = !ctx->is_left_handed;
        } else {
            int key_idx = input->key == InputKeyUp ? 0 : input->key == InputKeyLeft ? 1 : input->key == InputKeyRight ? 3 : input->key == InputKeyDown ? 4 : -1;
            if(key_idx >= 0) ctx->is_holding[key_idx] = is_press;
            if(is_short && input->key == InputKeyLeft && ctx->car_lane > 0 && (ctx->car_lane - 1) < ctx->active_lanes) {
                ctx->car_lane--;
                ctx->tap_count++;
                if(now - ctx->tap_window_start >= 60000) {
                    ctx->tap_count = 1;
                    ctx->tap_window_start = now;
                }
                float tap_bpm = (ctx->tap_count * 60000.0f) / (float)(now - ctx->tap_window_start + 1);
                if(fabsf(tap_bpm - (float)ctx->speed_bpm) < 5.0f) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
            }
            if(is_short && input->key == InputKeyRight && ctx->car_lane < 4 && (ctx->car_lane + 1) < ctx->active_lanes) {
                ctx->car_lane++;
                ctx->tap_count++;
                if(now - ctx->tap_window_start >= 60000) {
                    ctx->tap_count = 1;
                    ctx->tap_window_start = now;
                }
                float tap_bpm = (ctx->tap_count * 60000.0f) / (float)(now - ctx->tap_window_start + 1);
                if(fabsf(tap_bpm - (float)ctx->speed_bpm) < 5.0f) {
                    ctx->speed_bpm += 10;
                    if(ctx->speed_bpm > 120) ctx->speed_bpm = 120;
                }
            }
            if(is_press && input->key == InputKeyUp && ctx->car_y > 46 + (5 - ctx->active_lanes) * 6) {
                ctx->is_holding[0] = true;
            } else if(is_release && input->key == InputKeyUp) {
                ctx->is_holding[0] = false;
            }
            if(is_short && input->key == InputKeyDown && ctx->car_y < PORTRAIT_HEIGHT - 7) {
                ctx->car_y += 1;
            }
        }
    } else if(ctx->state == GAME_STATE_TECTONE_SIM) {
        if(is_short && input->key == InputKeyBack) {
            ctx->state = GAME_STATE_PAUSE;
            ctx->pause_back_count = 0;
            ctx->back_hold_start = 0;
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(input->key == InputKeyBack && now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
            ctx->is_left_handed = !ctx->is_left_handed;
        } else {
            if(is_press) {
                if(input->key == InputKeyLeft) ctx->is_holding[1] = true;
                else if(input->key == InputKeyDown) ctx->is_holding[4] = true;
                else if(input->key == InputKeyUp) ctx->is_holding[0] = true;
                else if(input->key == InputKeyRight) ctx->is_holding[3] = true;
                else if(input->key == InputKeyOk) ctx->is_holding[2] = true;
            } else if(is_release) {
                if(input->key == InputKeyLeft) ctx->is_holding[1] = false;
                else if(input->key == InputKeyDown) ctx->is_holding[4] = false;
                else if(input->key == InputKeyUp) ctx->is_holding[0] = false;
                else if(input->key == InputKeyRight) ctx->is_holding[3] = false;
                else if(input->key == InputKeyOk) ctx->is_holding[2] = false;
            }
        }
    } else if(ctx->state == GAME_STATE_SPACE_FLIGHT) {
        if(is_short && input->key == InputKeyBack) {
            ctx->state = GAME_STATE_PAUSE;
            ctx->pause_back_count = 0;
            ctx->back_hold_start = 0;
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(input->key == InputKeyBack && now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
            ctx->is_left_handed = !ctx->is_left_handed;
        } else {
            if(is_press) {
                if(input->key == InputKeyUp) ctx->screen_type = 1; // Upward
                else if(input->key == InputKeyDown) ctx->screen_type = 2; // Downward
                else if(input->key == InputKeyLeft) ctx->screen_type = 3; // Strife left
                else if(input->key == InputKeyRight) ctx->screen_type = 4; // Strife right
                else if(input->key == InputKeyOk) ctx->screen_type = 0; // Forward
            } else if(is_release) {
                if(input->key == InputKeyUp || input->key == InputKeyDown || input->key == InputKeyLeft ||
                   input->key == InputKeyRight || input->key == InputKeyOk) {
                    ctx->screen_type = 0; // Reset to forward on release
                }
            }
        }
    } else if(ctx->state == GAME_STATE_PAUSE) {
        if(is_short && input->key == InputKeyOk) {
            ctx->state = ctx->selected_game == GAME_MODE_ZERO_HERO ? GAME_STATE_ZERO_HERO :
                         ctx->selected_game == GAME_MODE_FLIP_ZIP ? GAME_STATE_FLIP_ZIP :
                         ctx->selected_game == GAME_MODE_LINE_CAR ? GAME_STATE_LINE_CAR :
                         ctx->selected_game == GAME_MODE_FLIP_IQ ? GAME_STATE_FLIP_IQ :
                         ctx->selected_game == GAME_MODE_TECTONE_SIM ? GAME_STATE_TECTONE_SIM :
                         ctx->selected_game == GAME_MODE_SPACE_FLIGHT ? GAME_STATE_SPACE_FLIGHT : GAME_STATE_ZERO_HERO;
            ctx->pause_back_count = 0;
        } else if(is_short && input->key == InputKeyBack) {
            ctx->pause_back_count++;
            if(ctx->pause_back_count >= 2) {
                ctx->state = GAME_STATE_TITLE;
                ctx->pause_back_count = 0;
            }
        } else if(is_press && input->key == InputKeyBack) {
            ctx->back_hold_start = now;
        } else if(is_release && input->key == InputKeyBack && ctx->back_hold_start > 0) {
            if(now - ctx->back_hold_start >= ORIENTATION_HOLD_MS) {
                ctx->is_left_handed = !ctx->is_left_handed;
            }
            ctx->back_hold_start = 0;
        }
    } else if(ctx->state == GAME_STATE_CREDITS) {
        if(is_short && input->key == InputKeyBack) {
            ctx->should_exit = true;
        }
    }
}

// Render callback for drawing all game states
static void render_callback(Canvas* canvas, void* ctx_ptr) {
    GameContext* ctx = ctx_ptr;
    if(!ctx || !ctx->view_port || !canvas) return;
    canvas_clear(canvas);
    if(ctx->state == GAME_STATE_LOADING) {
        view_port_set_orientation(ctx->view_port, ViewPortOrientationHorizontal);
        draw_loading_screen(canvas);
    } else if(ctx->state == GAME_STATE_TITLE) {
        view_port_set_orientation(ctx->view_port, ctx->is_left_handed ? ViewPortOrientationHorizontalFlip : ViewPortOrientationHorizontal);
        draw_title_menu(canvas, ctx);
    } else if(ctx->state == GAME_STATE_ROTATE) {
        draw_rotate_screen(canvas, ctx);
    } else if(ctx->state == GAME_STATE_CREDITS) {
        view_port_set_orientation(ctx->view_port, ctx->is_left_handed ? ViewPortOrientationHorizontalFlip : ViewPortOrientationHorizontal);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        canvas_set_color(canvas, ColorWhite);
        for(size_t i = 0; i < sizeof(credits_lines) / sizeof(credits_lines[0]); i++) {
            int y = ctx->credits_y - i * 10;
            if(y > -10 && y < SCREEN_HEIGHT) {
                int text_width = strlen(credits_lines[i]) * 8;
                int x = (SCREEN_WIDTH - text_width) / 2;
                draw_word_wrapped_text(canvas, credits_lines[i], x, y, SCREEN_WIDTH - 20, FontPrimary);
            }
        }
    } else if(ctx->state == GAME_STATE_PAUSE) {
        view_port_set_orientation(ctx->view_port, ctx->is_left_handed ? ViewPortOrientationVerticalFlip : ViewPortOrientationVertical);
        draw_pause_screen(canvas);
    } else {
        view_port_set_orientation(ctx->view_port, ctx->is_left_handed ? ViewPortOrientationVerticalFlip : ViewPortOrientationVertical);
        if(ctx->state == GAME_STATE_ZERO_HERO) {
            draw_zero_hero(canvas, ctx);
        } else if(ctx->state == GAME_STATE_FLIP_ZIP) {
            draw_flip_zip(canvas, ctx);
        } else if(ctx->state == GAME_STATE_LINE_CAR) {
            if(ctx->game_start_time == 0) {
                ctx->game_start_time = furi_get_tick(); // Set start time for title screen
            }
            uint32_t elapsed = furi_get_tick() - ctx->game_start_time;
            if(elapsed < 1300) {
                draw_line_car_title(canvas, ctx);
            } else {
                draw_line_car(canvas, ctx);
            }
        } else if(ctx->state == GAME_STATE_FLIP_IQ) {
            if(ctx->game_start_time == 0) {
                ctx->game_start_time = furi_get_tick(); // Set start time for title screen
            }
            uint32_t elapsed = furi_get_tick() - ctx->game_start_time;
            if(elapsed < 1300) {
                draw_flip_iq_title(canvas, ctx);
            } else {
                // Draw background and game board
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_box(canvas, 0, 26, PORTRAIT_WIDTH, 20); // Background screen
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_box(canvas, 0, 46, PORTRAIT_WIDTH, PORTRAIT_HEIGHT - 53); // Game board
                // Draw inactive lanes
                for(int i = ctx->active_lanes; i < 5; i++) {
                    canvas_set_color(canvas, ColorBlack);
                    canvas_draw_box(canvas, i * 12, 46, 12, PORTRAIT_HEIGHT - 53);
                }
                // Draw balls with break effect
                for(int i = 0; i < 5; i++) {
                    for(int j = 0; j < WORLD_OBJ_LIMIT; j++) {
                        if(ctx->key_positions[i][j] > 0 && ctx->key_positions[i][j] < PORTRAIT_HEIGHT - 7) {
                            canvas_set_color(canvas, ColorWhite);
                            canvas_draw_frame(canvas, i * 12 + 4, ctx->key_positions[i][j] - ctx->key_columns[i][j] / 2, ctx->key_columns[i][j], ctx->key_columns[i][j]);
                            canvas_set_color(canvas, ColorBlack);
                            if(ctx->ball_broken[j]) {
                                canvas_draw_box(canvas, i * 12 + 4, ctx->key_positions[i][j], ctx->key_columns[i][j], ctx->key_columns[i][j] / 2); // Dither effect
                            } else {
                                canvas_draw_disc(canvas, i * 12 + 6, ctx->key_positions[i][j], ctx->key_columns[i][j] / 2);
                            }
                        }
                    }
                }
                // Draw player
                canvas_draw_box(canvas, ctx->car_lane * 12 + 4, ctx->car_y, 2, 3); // Body
                canvas_draw_disc(canvas, ctx->car_lane * 12 + 5, ctx->car_y - 1, 1); // Head
                canvas_draw_frame(canvas, ctx->car_lane * 12 + 3, ctx->car_y - 1, 4, 4); // Border
                // Timer in marquee
                if(ctx->notification_text[0] == '\0' && ctx->game_start_time > 0) {
                    uint32_t elapsed = (furi_get_tick() - ctx->round_start_time) / 1000;
                    uint32_t minutes = elapsed / 60;
                    uint32_t seconds = elapsed % 60;
                    char timer_str[12];
                    snprintf(timer_str, sizeof(timer_str), "%02lu:%02lu", minutes, seconds);
                    draw_word_wrapped_text(canvas, timer_str, (PORTRAIT_WIDTH - strlen(timer_str) * 6) / 2, PORTRAIT_HEIGHT - 1, PORTRAIT_WIDTH, FontSecondary);
                }
                // Death screen
                if(ctx->car_y > 46 + (5 - ctx->active_lanes) * 6 && ctx->state != GAME_STATE_TITLE) {
                    float gpa_to_iq = ((float)ctx->score / (float)(ctx->difficulty + 2) * 0.333f) * 100.0f;
                    canvas_set_color(canvas, ColorBlack);
                    canvas_draw_box(canvas, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
                    canvas_set_color(canvas, ColorWhite);
                    draw_word_wrapped_text(canvas, "DEAD TOTAL", 10, 20, PORTRAIT_WIDTH - 20, FontPrimary);
                    char score_str[32];
                    snprintf(score_str, sizeof(score_str), "%d PP", ctx->score);
                    draw_word_wrapped_text(canvas, score_str, 10, 30, PORTRAIT_WIDTH - 20, FontPrimary);
                    draw_word_wrapped_text(canvas, "    ", 10, 40, PORTRAIT_WIDTH - 20, FontPrimary);
                    draw_word_wrapped_text(canvas, "YOUR IQ IS:", 10, 50, PORTRAIT_WIDTH - 20, FontPrimary);
                    char iq_str[32];
                    snprintf(iq_str, sizeof(iq_str), "%.1f", (double)gpa_to_iq);
                    draw_word_wrapped_text(canvas, iq_str, 10, 60, PORTRAIT_WIDTH - 20, FontPrimary);
                    if(furi_get_tick() - ctx->last_notification_time > 1500) {
                        ctx->score += ctx->score; // Add PP to total score
                        ctx->state = GAME_STATE_TITLE;
                    }
                }
            }
        } else if(ctx->state == GAME_STATE_TECTONE_SIM) {
            // Draw bedroom
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_box(canvas, 0, 47, PORTRAIT_WIDTH, 21); // Wall
            if(ctx->is_day) {
                for(int i = 0; i < 4; i++) {
                    canvas_draw_frame(canvas, 10 + i * 12, 50, 10, 10); // Window squares
                }
            } else {
                canvas_draw_box(canvas, 10, 50, 48, 10); // Black window
            }
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, 53, PORTRAIT_WIDTH, 6); // Desk
            canvas_draw_box(canvas, PORTRAIT_WIDTH - 12, 47, 12, 9); // Monitor
            // Draw Tectone (bongo cat style)
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_disc(canvas, ctx->tectone_x + 5, 50, 5); // Head
            canvas_draw_line(canvas, ctx->tectone_x + 3, 55, ctx->tectone_x + 7, 55); // Mouth
            canvas_draw_dot(canvas, ctx->tectone_x + 4, 49); // Left eye
            canvas_draw_dot(canvas, ctx->tectone_x + 6, 49); // Right eye
            int frame = (furi_get_tick() / 200) % 2;
            if(frame == 0) {
                canvas_draw_line(canvas, ctx->tectone_x + 4, 49, ctx->tectone_x + 6, 49); // Closed eyes
            }
            canvas_draw_disc(canvas, ctx->tectone_x + 2, 57, 2); // Left hand
            canvas_draw_disc(canvas, ctx->tectone_x + 8, 57, 2); // Right hand
            if((furi_get_tick() / 300) % 2 == 0) {
                canvas_draw_box(canvas, ctx->tectone_x + 2, 57, 2, 2); // Left hand down
                canvas_draw_disc(canvas, ctx->tectone_x + 8, 55, 2); // Right hand up
            } else {
                canvas_draw_disc(canvas, ctx->tectone_x + 2, 55, 2); // Left hand up
                canvas_draw_box(canvas, ctx->tectone_x + 8, 57, 2, 2); // Right hand down
            }
            // Draw props based on last action
            if(ctx->is_holding[4]) { // Down: Prop
                int prop = rand() % 3;
                if(prop == 0) { // Microphone
                    canvas_draw_str(canvas, ctx->tectone_x + 4, 52, "i");
                } else if(prop == 1) { // Shotgun
                    canvas_draw_str(canvas, ctx->tectone_x + 4, 52, "F");
                    canvas_draw_str(canvas, ctx->tectone_x + 4, 50, "F");
                } else { // Ball
                    canvas_draw_disc(canvas, ctx->tectone_x + 5, 52, 2);
                }
            }
            // Draw button area
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_box(canvas, 0, 68, PORTRAIT_WIDTH, 20);
            canvas_set_color(canvas, ColorBlack);
            draw_word_wrapped_text(canvas, "< : ANGER", 5, 70, 30, FontSecondary);
            draw_word_wrapped_text(canvas, "\\/ : PROP", 40, 70, 30, FontSecondary);
            draw_word_wrapped_text(canvas, "^ : BASED", 5, 80, 30, FontSecondary);
            draw_word_wrapped_text(canvas, "> : UWU", 40, 80, 30, FontSecondary);
            if(ctx->is_holding[1]) canvas_draw_frame(canvas, 5, 70, 10, 10); // Anger button
            if(ctx->is_holding[4]) canvas_draw_frame(canvas, 40, 70, 10, 10); // Prop button
            if(ctx->is_holding[0]) canvas_draw_frame(canvas, 5, 80, 10, 10); // Based button
            if(ctx->is_holding[3]) canvas_draw_frame(canvas, 40, 80, 10, 10); // UWU button
            // Draw comments
            for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
                if(ctx->comment_positions[i] > 0) {
                    canvas_set_color(canvas, i % 2 ? ColorWhite : ColorBlack);
                    canvas_draw_frame(canvas, 0, ctx->comment_positions[i], PORTRAIT_WIDTH, ctx->comment_heights[i]);
                    canvas_set_color(canvas, i % 2 ? ColorBlack : ColorWhite);
                    char comment[32];
                    snprintf(comment, sizeof(comment), "%s%s%s%s", tectone_starters[rand() % 4], tectone_subjects[rand() % 4], tectone_climaxes[rand() % 5], tectone_endpoints[rand() % 5]);
                    draw_word_wrapped_text(canvas, comment, 5, ctx->comment_positions[i] + 2, PORTRAIT_WIDTH - 10, FontSecondary);
                }
            }
            draw_notification(canvas, ctx);
        } else if(ctx->state == GAME_STATE_SPACE_FLIGHT) {
            if(ctx->game_start_time == 0) {
                ctx->game_start_time = furi_get_tick(); // Set start time for title screen
            }
            uint32_t elapsed = furi_get_tick() - ctx->game_start_time;
            if(elapsed < 1300) {
                draw_space_flight_title(canvas, ctx);
            } else {
                // Draw HUD
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_box(canvas, 0, 26, PORTRAIT_WIDTH, 10);
                char health_str[16];
                snprintf(health_str, sizeof(health_str), "[♥]: %d", ctx->ship_health);
                draw_word_wrapped_text(canvas, health_str, 5, 32, 32, FontSecondary);
                char armor_str[16];
                snprintf(armor_str, sizeof(armor_str), "%d :[◯]", ctx->ship_armor);
                draw_word_wrapped_text(canvas, armor_str, 40, 32, 32, FontSecondary);
                // Draw player view
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_box(canvas, 0, 36, PORTRAIT_WIDTH, 65); // Adjusted to 65 pixels
                canvas_set_color(canvas, ColorWhite);
                for(int i = 0; i < WORLD_OBJ_LIMIT; i++) {
                    if(ctx->objects[i][2] > 0) {
                        int size = ctx->objects[i][2] * (PORTRAIT_HEIGHT - ctx->objects[i][1]) / 100; // Scale based on distance
                        canvas_draw_disc(canvas, ctx->objects[i][0], ctx->objects[i][1], size);
                    } else if(ctx->objects[i][2] < 0) {
                        canvas_draw_circle(canvas, ctx->objects[i][0], ctx->objects[i][1], abs(ctx->objects[i][2])); // Pickup
                    }
                }
                // Draw user panel
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_box(canvas, 0, 101, PORTRAIT_WIDTH, 10); // Adjusted to 10 pixels
                canvas_set_color(canvas, ColorBlack);
                if(ctx->screen_type == 5) canvas_draw_disc(canvas, 10, 105, 3); // Back loop light
                if(ctx->screen_type == 6) canvas_draw_disc(canvas, 54, 105, 3); // Barrel roll light
                canvas_draw_frame(canvas, 22, 102, 6, 6); // Up button
                canvas_draw_frame(canvas, 30, 102, 6, 6); // Down button
                canvas_draw_frame(canvas, 14, 102, 6, 6); // Left button
                canvas_draw_frame(canvas, 38, 102, 6, 6); // Right button
                if(ctx->is_holding[0]) canvas_draw_box(canvas, 22, 102, 6, 6);
                if(ctx->is_holding[4]) canvas_draw_box(canvas, 30, 102, 6, 6);
                if(ctx->is_holding[1]) canvas_draw_box(canvas, 14, 102, 6, 6);
                if(ctx->is_holding[3]) canvas_draw_box(canvas, 38, 102, 6, 6);
                draw_notification(canvas, ctx);
            }
        }
    }
}

// Timer callback for faux multithreading
static void timer_callback(void* ctx_ptr) {
    GameContext* ctx = ctx_ptr;
    if(!ctx) return;
    uint32_t now = furi_get_tick();
    ctx->frame_counter = (ctx->frame_counter + 1) % 3;

    // Faux multithreading: 3-frame cycle
    if(ctx->frame_counter == 0) {
        // Frame 1: Process input (handled in input_callback)
    } else if(ctx->frame_counter == 1) {
        // Frame 2: Process notification text
        if(ctx->notification_text[0] != '\0' && ctx->note_q_a == 0) {
            uint32_t elapsed = now - ctx->last_notification_time;
            if(elapsed < NOTIFICATION_MS) {
                int text_width = strlen(ctx->notification_text) * 6;
                ctx->notification_x = (PORTRAIT_WIDTH - text_width) / 2 - (elapsed * text_width / NOTIFICATION_MS);
                if(ctx->notification_x < -text_width) ctx->notification_x += text_width;
            } else {
                ctx->notification_text[0] = '\0';
                ctx->notification_x = 0;
            }
        }
    } else if(ctx->frame_counter == 2) {
        // Frame 3: Process game updates
        if(ctx->state == GAME_STATE_ZERO_HERO) {
            update_zero_hero(ctx);
        } else if(ctx->state == GAME_STATE_FLIP_ZIP) {
            update_flip_zip(ctx);
        } else if(ctx->state == GAME_STATE_LINE_CAR) {
            update_line_car(ctx);
        } else if(ctx->state == GAME_STATE_FLIP_IQ) {
            update_flip_iq(ctx);
        } else if(ctx->state == GAME_STATE_TECTONE_SIM) {
            update_tectone_sim(ctx);
        } else if(ctx->state == GAME_STATE_SPACE_FLIGHT) {
            update_space_flight(ctx);
        }
    }

    // Common updates
    if(now > ctx->day_night_toggle_time) {
        ctx->is_day = !ctx->is_day;
        ctx->day_night_toggle_time = now + 300000;
    }
    if(ctx->state == GAME_STATE_CREDITS) {
        static uint32_t last_credits_update = 0;
        if(now - last_credits_update >= 85) {
            ctx->credits_y--;
            last_credits_update = now;
        }
        if(ctx->credits_y < -10) {
            ctx->should_exit = true;
        }
    }
}

// Main application entry point
int32_t nah2nah3_app(void* p) {
    UNUSED(p);
    // Allocate game context
    GameContext* ctx = malloc(sizeof(GameContext));
    if(!ctx) return -1;
    memset(ctx, 0, sizeof(GameContext));
    ctx->state = GAME_STATE_LOADING;
    ctx->game_start_time = furi_get_tick();
    ctx->is_day = true;
    ctx->day_night_toggle_time = furi_get_tick() + 300000;
    ctx->mascot_lane = 2;
    ctx->streak = 0; // Initialize streak to 0
    srand(furi_get_tick());

    // Initialize GUI with extended delay for stability
    Gui* gui = furi_record_open(RECORD_GUI);
    if(!gui) {
        free(ctx);
        return -1;
    }
    ViewPort* view_port = view_port_alloc();
    if(!view_port) {
        furi_record_close(RECORD_GUI);
        free(ctx);
        return -1;
    }
    ctx->view_port = view_port;
    view_port_draw_callback_set(view_port, render_callback, ctx);
    view_port_input_callback_set(view_port, input_callback, ctx);
    view_port_set_orientation(view_port, ViewPortOrientationHorizontal);
    furi_delay_ms(500); // Extended delay to stabilize GUI
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    furi_delay_ms(100); // Additional delay post-add

    // Speaker setup for SAM
    #if USE_SAM_TTS
    sam_init(&voice);
    #endif // USE_SAM_TTS

    // Start timer
    FuriTimer* timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, ctx);
    if(!timer) {
        view_port_free(view_port);
        furi_record_close(RECORD_GUI);
        free(ctx);
        return -1;
    }
    if(furi_timer_start(timer, 1000 / FPS_BASE) != FuriStatusOk) {
        furi_timer_free(timer);
        view_port_free(view_port);
        furi_record_close(RECORD_GUI);
        free(ctx);
        return -1;
    }

    // Main loop with loading screen transition
    while(!ctx->should_exit) {
        if(ctx->state == GAME_STATE_LOADING && furi_get_tick() - ctx->game_start_time >= LOADING_MS) {
            ctx->state = GAME_STATE_TITLE;
            ctx->selected_side = 0;
            ctx->selected_row = 0;
            ctx->title_scroll_offset = 0;
            view_port_input_callback_set(ctx->view_port, input_callback, ctx); // Re-register input
        }
        furi_delay_ms(100);
    }

    // Cleanup
    if(timer) {
        furi_timer_stop(timer);
        furi_timer_free(timer);
    }
    if(view_port) {
        gui_remove_view_port(gui, view_port);
        view_port_draw_callback_set(view_port, NULL, NULL);
        view_port_input_callback_set(view_port, NULL, NULL);
        view_port_free(view_port);
    }
    if(gui) {
        furi_record_close(RECORD_GUI);
    }
    if(ctx) {
        free(ctx);
    }
    return 0;
}