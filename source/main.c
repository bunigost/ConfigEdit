#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "logo.h"

// Constants
#define SCREEN_LINES      24      // Number of visible lines on screen
#define TOP_MARGIN 3      // Top margin for header
#define MAX_VISIBLE_LINES (SCREEN_LINES - TOP_MARGIN)

// Browser
#define MAX_ENTRIES       512     // Max number of directory entries
#define MAX_PATH_LEN      256     // Max length for file paths

#define SKIP_LINES        20      // Number of lines to skip on left/right key press
#define BROWSER_REPEAT_DELAY 15
#define BROWSER_REPEAT_RATE 3

// Editor
#define MAX_LINES         1024    // Max lines in a text file
#define MAX_LINE_LENGTH   256     // Max length of a single line

#define EDITOR_REPEAT_DELAY 20
#define EDITOR_REPEAT_RATE 4

// Global state variables
int scroll_offset = 0;            // Current scroll offset in directory listing
int repeat_counter = 0;           // Counter for key repeat timing
int repeat_direction = 0;         // Direction of repeat: -1 (up), +1 (down), 0 (none)

// Directory entry structure
typedef struct {
    char name[256];               // Entry name
    bool is_dir;                  // True if directory
} Entry;

// Directory entries array and count
Entry entries[MAX_ENTRIES];
int entry_count = 0;
char current_path[MAX_PATH_LEN] = "/";

// Initialize console on top screen
void init_top_console(void) {
    videoSetMode(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
}

// Initialize console on bottom screen
void init_bottom_console(void) {
    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
}

void show_logo_on_top_screen() {
    videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);

    BGCTRL[0] = BG_TILE_BASE(1) | BG_MAP_BASE(0) | BG_COLOR_256 | BG_32x32;

    dmaCopy(logoTiles, (void*)CHAR_BASE_BLOCK(1), logoTilesLen);
    dmaCopy(logoMap, (void*)SCREEN_BASE_BLOCK(0), logoMapLen);
    dmaCopy(logoPal, BG_PALETTE, logoPalLen);
}

bool is_supported_file(const char *filename) {
    size_t len = strlen(filename);
    if (len < 5) return false;  // Minimal length for extensions like ".ini"

    const char *ext = strrchr(filename, '.');
    if (!ext) return false;

    return (
        strcasecmp(ext, ".ini") == 0 ||
        strcasecmp(ext, ".cfg") == 0 ||
        strcasecmp(ext, ".txt") == 0 ||
        strcasecmp(ext, ".json") == 0 ||
        strcasecmp(ext, ".xml") == 0
    );
}

void read_directory(const char *path) {
    DIR *pdir = opendir(path);
    if (!pdir) {
        iprintf("Failed to open directory: %s\n", path);
        entry_count = 0;
        return;
    }

    struct dirent *pent;
    entry_count = 0;

    while ((pent = readdir(pdir)) != NULL && entry_count < MAX_ENTRIES) {
        if (strcmp(".", pent->d_name) == 0 || strcmp("..", pent->d_name) == 0)
            continue;

        bool is_dir = (pent->d_type == DT_DIR);

        // Skip unsupported files if not a directory
        if (!is_dir && !is_supported_file(pent->d_name)) continue;

        strncpy(entries[entry_count].name, pent->d_name, sizeof(entries[entry_count].name) - 1);
        entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';

        entries[entry_count].is_dir = is_dir;
        entry_count++;
    }

    closedir(pdir);

    // Sort entries: directories first, then files alphabetically
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = i + 1; j < entry_count; j++) {
            if (!entries[i].is_dir && entries[j].is_dir) {
                Entry temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            } else if (entries[i].is_dir == entries[j].is_dir &&
                       strcasecmp(entries[i].name, entries[j].name) > 0) {
                Entry temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
        }
    }
}

void draw_directory(int cursor, int scroll_offset) {
    consoleClear();

    // Print header at fixed position (line 0)
    iprintf("\x1b[1;1H%s\n", current_path);

    // Calculate visible range based on scroll
    int start = scroll_offset;
    int end = (start + MAX_VISIBLE_LINES < entry_count) ? start + MAX_VISIBLE_LINES : entry_count;

    // Print each directory entry on its own line, starting at line TOP_MARGIN (1-based)
    for (int i = start; i < end; i++) {
        int line_num = i - start + TOP_MARGIN + 1;  // +1 because ANSI lines start at 1
        iprintf("\x1b[%d;1H", line_num);

        // Clear the entire line to avoid leftover characters
        iprintf("\x1b[2K");  // ANSI escape to clear entire line

        // Print cursor or spaces
        if (i == cursor) {
            // Highlight cursor
            iprintf("> ");

            // Print directory or file name
            if (entries[i].is_dir) {
                iprintf("[%s]", entries[i].name);
            } else {
                iprintf("%s", entries[i].name);
            }

            // Print highlight cursor at the end
            iprintf(" <");
        } else {
            // No cursor, indent space
            iprintf("  ");
            if (entries[i].is_dir) {
                iprintf("[%s]", entries[i].name);
            } else {
                iprintf("%s", entries[i].name);
            }
        }
    }
}

// Navigate one directory up in current_path
void go_up_directory() {
    if (strcmp(current_path, "/") == 0) return;

    size_t len = strlen(current_path);
    if (current_path[len - 1] == '/') {
        current_path[len - 1] = '\0';
        len--;
    }

    char *last_slash = strrchr(current_path, '/');
    if (last_slash) {
        if (last_slash == current_path) {
            last_slash[1] = '\0';  // Keep root "/"
        } else {
            *last_slash = '\0';    // Trim to parent directory
        }
    }
}

void save_file(const char *filepath, char file_lines[][MAX_LINE_LENGTH], int total_lines) {
    FILE *file = fopen(filepath, "w");
    if (!file) return;

    for (int i = 0; i < total_lines; i++) {
        fprintf(file, "%s\n", file_lines[i]);
    }
    fclose(file);
}

// Draw a single line with cursor shown at cursor_x on cursor_y line
void draw_line_with_cursor(int cursor_x, int cursor_y, int line_index, const char* line) {

    // Print the text line
    iprintf("%s\n", line);

    // If this is the cursor line, print a line with spaces and underscore at cursor_x
    if (line_index == cursor_y) {
        for (int i = 0; i < cursor_x; i++) iprintf(" ");
        iprintf("^\n"); 
    }
}

void view_text_file(const char *filepath) {
    init_top_console();

    FILE *file = fopen(filepath, "r");
    if (!file) {
        consoleClear();
        iprintf("Failed to open file:\n%s\nPress B to return.", filepath);
        while (1) {
            scanKeys();
            if (keysDown() & KEY_B) break;
            swiWaitForVBlank();
        }
        return;
    }

    static char file_lines[MAX_LINES][MAX_LINE_LENGTH];
    int total_lines = 0;

    while (fgets(file_lines[total_lines], MAX_LINE_LENGTH, file) && total_lines < MAX_LINES) {
        size_t len = strlen(file_lines[total_lines]);
        if (len > 0 && (file_lines[total_lines][len - 1] == '\n' || file_lines[total_lines][len - 1] == '\r'))
            file_lines[total_lines][len - 1] = '\0';
        total_lines++;
    }
    fclose(file);

    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);

    keyboardDemoInit();
    keyboardShow();

    int cursor_x = 0, cursor_y = 0, scroll = 0;
    int repeat_direction = 0;  // 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 0=none
    int repeat_counter = 0;

     
    while (1) {
        consoleClear();

        iprintf("\x1b[1;1H %s", filepath); // draw header at line 0


        if (cursor_y < scroll)
            scroll = cursor_y;
        if (cursor_y >= scroll + MAX_VISIBLE_LINES)
            scroll = cursor_y - MAX_VISIBLE_LINES + 1;

for (int i = 0; i < MAX_VISIBLE_LINES; i++) {
    int line_index = scroll + i;
    if (line_index >= total_lines) break;
    const char *line = file_lines[line_index];

    // Move to the correct screen line (with offset)
    iprintf("\x1b[%d;1H", i + TOP_MARGIN + 1); // ANSI is 1-indexed

    if (line_index == cursor_y) {
        for (int c = 0; c < cursor_x && line[c] != '\0'; c++)
            iprintf("%c", line[c]);
        iprintf("_");
        iprintf("%s", &line[cursor_x]);
    } else {
        iprintf("%s", line);
    }
}


        int key = keyboardUpdate();
        if (key > 0) {
            char *line = file_lines[cursor_y];
            int len = strlen(line);

            if (key == 8) { // Backspace
                if (cursor_x > 0) {
                    memmove(&line[cursor_x - 1], &line[cursor_x], len - cursor_x + 1);
                    cursor_x--;
                } else if (cursor_y > 0) {
                    int prev_len = strlen(file_lines[cursor_y - 1]);
                    int curr_len = strlen(file_lines[cursor_y]);
                    if (prev_len + curr_len < MAX_LINE_LENGTH) {
                        strcat(file_lines[cursor_y - 1], file_lines[cursor_y]);
                        for (int l = cursor_y; l < total_lines - 1; l++)
                            strcpy(file_lines[l], file_lines[l + 1]);
                        total_lines--;
                        cursor_y--;
                        cursor_x = prev_len;
                    }
                }
            } else if (key == 13) { // Enter
                if (total_lines < MAX_LINES) {
                    char tail[MAX_LINE_LENGTH] = {0};
                    strcpy(tail, &line[cursor_x]);
                    line[cursor_x] = '\0';

                    for (int l = total_lines; l > cursor_y + 1; l--)
                        strcpy(file_lines[l], file_lines[l - 1]);

                    strcpy(file_lines[cursor_y + 1], tail);
                    total_lines++;
                    cursor_y++;
                    cursor_x = 0;
                }
            } else if (key >= 32 && key <= 126) { // Printable chars
                if (len < MAX_LINE_LENGTH - 1) {
                    memmove(&line[cursor_x + 1], &line[cursor_x], len - cursor_x + 1);
                    line[cursor_x] = (char)key;
                    cursor_x++;
                }
            }
        }

        scanKeys();
        int keys_down = keysDown();
        int keys_held = keysHeld();

        if ((keys_down & KEY_UP) || (keys_held & KEY_UP && repeat_direction == 1)) {
            if (keys_down & KEY_UP) {
                if (cursor_y > 0) {
                    cursor_y--;
                    int line_len = strlen(file_lines[cursor_y]);
                    if (cursor_x > line_len) cursor_x = line_len;
                }
                repeat_direction = 1;
                repeat_counter = 0;
            } else {
                repeat_counter++;
                if (repeat_counter >= EDITOR_REPEAT_DELAY && (repeat_counter - EDITOR_REPEAT_DELAY) % EDITOR_REPEAT_RATE == 0) {
                    if (cursor_y > 0) {
                        cursor_y--;
                        int line_len = strlen(file_lines[cursor_y]);
                        if (cursor_x > line_len) cursor_x = line_len;
                    }
                }
            }
        } else if ((keys_down & KEY_DOWN) || (keys_held & KEY_DOWN && repeat_direction == 2)) {
            if (keys_down & KEY_DOWN) {
                if (cursor_y < total_lines - 1) {
                    cursor_y++;
                    int line_len = strlen(file_lines[cursor_y]);
                    if (cursor_x > line_len) cursor_x = line_len;
                }
                repeat_direction = 2;
                repeat_counter = 0;
            } else {
                repeat_counter++;
                if (repeat_counter >= EDITOR_REPEAT_DELAY && (repeat_counter - EDITOR_REPEAT_DELAY) % EDITOR_REPEAT_RATE == 0) {
                    if (cursor_y < total_lines - 1) {
                        cursor_y++;
                        int line_len = strlen(file_lines[cursor_y]);
                        if (cursor_x > line_len) cursor_x = line_len;
                    }
                }
            }
        } else if ((keys_down & KEY_LEFT) || (keys_held & KEY_LEFT && repeat_direction == 3)) {
            if (keys_down & KEY_LEFT) {
                if (cursor_x > 0) cursor_x--;
                else if (cursor_y > 0) {
                    cursor_y--;
                    cursor_x = strlen(file_lines[cursor_y]);
                }
                repeat_direction = 3;
                repeat_counter = 0;
            } else {
                repeat_counter++;
                if (repeat_counter >= EDITOR_REPEAT_DELAY && (repeat_counter - EDITOR_REPEAT_DELAY) % EDITOR_REPEAT_RATE == 0) {
                    if (cursor_x > 0) cursor_x--;
                    else if (cursor_y > 0) {
                        cursor_y--;
                        cursor_x = strlen(file_lines[cursor_y]);
                    }
                }
            }
        } else if ((keys_down & KEY_RIGHT) || (keys_held & KEY_RIGHT && repeat_direction == 4)) {
            if (keys_down & KEY_RIGHT) {
                int line_len = strlen(file_lines[cursor_y]);
                if (cursor_x < line_len) cursor_x++;
                else if (cursor_y < total_lines - 1) {
                    cursor_y++;
                    cursor_x = 0;
                }
                repeat_direction = 4;
                repeat_counter = 0;
            } else {
                repeat_counter++;
                if (repeat_counter >= EDITOR_REPEAT_DELAY && (repeat_counter - EDITOR_REPEAT_DELAY) % EDITOR_REPEAT_RATE == 0) {
                    int line_len = strlen(file_lines[cursor_y]);
                    if (cursor_x < line_len) cursor_x++;
                    else if (cursor_y < total_lines - 1) {
                        cursor_y++;
                        cursor_x = 0;
                    }
                }
            }
        } else {
            repeat_direction = 0;
            repeat_counter = 0;
        }

        if (keys_down & KEY_START) {
            save_file(filepath, file_lines, total_lines);
            consoleClear();
            iprintf("File saved!\nPress B to exit or continue editing.");
            while (1) {
                scanKeys();
                if (keysDown() & KEY_B) break;
                swiWaitForVBlank();
            }
            consoleClear();
        }

        if (keys_down & KEY_B) break;

        swiWaitForVBlank();
    }

    keyboardHide();
    show_logo_on_top_screen();
}

int main(int argc, char **argv) {
    show_logo_on_top_screen();

    consoleDemoInit();

    if (!fatInitDefault()) {
        iprintf("fatInitDefault fail: terminating\n");
        while (1)
        swiWaitForVBlank();
    }

    read_directory(current_path);

    int cursor = 0;
    int scroll_offset = 0;

    int repeat_counter = 0;
    int repeat_direction = 0;  // -1 = up, 1 = down, 0 = none, -2 = left skip, 2 = right skip

    // Clamp scroll_offset to valid range
    void clamp_scroll_offset() {
        if (scroll_offset < 0) scroll_offset = 0;
        int max_scroll = entry_count - (SCREEN_LINES - 2);
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    }

    while (1) {
        scanKeys();
        int keys_down = keysDown();
        int keys_held = keysHeld();

        // Handle UP key with repeat
        if ((keys_down & KEY_UP) || (keys_held & KEY_UP && repeat_direction == -1)) {
            if (keys_down & KEY_UP) {
                cursor = (cursor > 0) ? cursor - 1 : 0;
                if (cursor < scroll_offset) scroll_offset--;
                repeat_direction = -1;
                repeat_counter = 0;
            } else if (keys_held & KEY_UP) {
                repeat_counter++;
                if (repeat_counter >= BROWSER_REPEAT_DELAY) {
                    if ((repeat_counter - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0) {
                        cursor = (cursor > 0) ? cursor - 1 : 0;
                        if (cursor < scroll_offset) scroll_offset--;
                    }
                }
            }
        }
        // Handle DOWN key with repeat
        else if ((keys_down & KEY_DOWN) || (keys_held & KEY_DOWN && repeat_direction == 1)) {
            if (keys_down & KEY_DOWN) {
                cursor = (cursor < entry_count - 1) ? cursor + 1 : entry_count - 1;
                if (cursor >= scroll_offset + (SCREEN_LINES - 3)) scroll_offset++;
                repeat_direction = 1;
                repeat_counter = 0;
            } else if (keys_held & KEY_DOWN) {
                repeat_counter++;
                if (repeat_counter >= BROWSER_REPEAT_DELAY) {
                    if ((repeat_counter - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0) {
                        cursor = (cursor < entry_count - 1) ? cursor + 1 : entry_count - 1;
                        if (cursor >= scroll_offset + (SCREEN_LINES - 3)) scroll_offset++;
                    }
                }
            }
        }
        // Handle LEFT key with repeat (skip up)
        else if ((keys_down & KEY_LEFT) || (keys_held & KEY_LEFT && repeat_direction == -2)) {
            if (keys_down & KEY_LEFT) {
                cursor -= SKIP_LINES;
                if (cursor < 0) cursor = 0;
                if (cursor < scroll_offset) scroll_offset = cursor;
                repeat_direction = -2;
                repeat_counter = 0;
            } else if (keys_held & KEY_LEFT) {
                repeat_counter++;
                if (repeat_counter >= BROWSER_REPEAT_DELAY) {
                    if ((repeat_counter - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0) {
                        cursor -= SKIP_LINES;
                        if (cursor < 0) cursor = 0;
                        if (cursor < scroll_offset) scroll_offset = cursor;
                    }
                }
            }
        }
        // Handle RIGHT key with repeat (skip down)
        else if ((keys_down & KEY_RIGHT) || (keys_held & KEY_RIGHT && repeat_direction == 2)) {
            if (keys_down & KEY_RIGHT) {
                cursor += SKIP_LINES;
                if (cursor >= entry_count) cursor = entry_count - 1;
                if (cursor >= scroll_offset + (SCREEN_LINES - 3)) scroll_offset = cursor - (SCREEN_LINES - 3) + 1;
                repeat_direction = 2;
                repeat_counter = 0;
            } else if (keys_held & KEY_RIGHT) {
                repeat_counter++;
                if (repeat_counter >= BROWSER_REPEAT_DELAY) {
                    if ((repeat_counter - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0) {
                        cursor += SKIP_LINES;
                        if (cursor >= entry_count) cursor = entry_count - 1;
                        if (cursor >= scroll_offset + (SCREEN_LINES - 3)) scroll_offset = cursor - (SCREEN_LINES - 3) + 1;
                    }
                }
            }
        }
        else {
            // No up/down/left/right key held, reset repeat state
            repeat_direction = 0;
            repeat_counter = 0;
        }

        // Clamp scroll_offset after any movement
        clamp_scroll_offset();

        if (keys_down & KEY_START) {
            break;
        }

        if (keys_down & KEY_A) {
            if (entries[cursor].is_dir) {
                char new_path[MAX_PATH_LEN];
                if (strcmp(current_path, "/") == 0) {
                    snprintf(new_path, MAX_PATH_LEN, "/%s", entries[cursor].name);
                } else {
                    snprintf(new_path, MAX_PATH_LEN, "%s/%s", current_path, entries[cursor].name);
                }
                strncpy(current_path, new_path, MAX_PATH_LEN - 1);
                current_path[MAX_PATH_LEN - 1] = '\0';
                read_directory(current_path);
                cursor = 0;
                scroll_offset = 0;
                draw_directory(cursor, scroll_offset);
            } else {
                const char *filename = entries[cursor].name;
                if (is_supported_file(filename)) {
                    char filepath[MAX_PATH_LEN];
                    snprintf(filepath, sizeof(filepath), "%s%s%s",
                             current_path,
                             (strcmp(current_path, "/") == 0) ? "" : "/",
                             filename);
                    view_text_file(filepath);
                    consoleDemoInit();
                    draw_directory(cursor, scroll_offset);
                }
            }
        }

        if (keys_down & KEY_B) {
            go_up_directory();
            read_directory(current_path);
            cursor = 0;
            scroll_offset = 0;
        }

        consoleClear();

draw_directory(cursor, scroll_offset);


        swiWaitForVBlank();
    }

    return 0;
}
