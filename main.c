#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

enum Mode {
    NORMAL,
    INSERT,
    VISUAL
};

typedef struct {
    char *buf;
    int len;
} Line;

typedef struct {
    Line *lines;
    int len;
} TextBody;

// rows and cols available on current window size
static int rows, cols;

// user row and col cursor positions
static int cursor_row = 1;
static int cursor_col = 1;

static struct termios orig_termios;
enum Mode mode = NORMAL;

void clear_screen() {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void enter_alternate_screen(void) {
    printf("\033[?1049h");
    fflush(stdout);
}

void exit_alternate_screen(void) {
    printf("\033[?1049l");
    fflush(stdout);
}

void get_terminal_size() {
    struct winsize w;

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    rows = w.ws_row;
    cols = w.ws_col;
}

char read_key(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return c;
    return '\0';
}

void move_to_row_col(const int row, const int col) {
    printf("\033[%d;%dH", row, col);
}

void print_in_row_col(const char *str, const int row, const int col) {
    move_to_row_col(row, col);
    printf("%s", str);
}

void format_win_dims(char *out, size_t out_size, const int dim_one, const int dim_two) {
    snprintf(out, out_size, "%d x %d", rows, cols);
}

void populate_lines(TextBody *text_body, char *buf) {
    text_body->len = 0;

    int index = 0;
    const char *tok = strtok(buf, "\n");
    while (tok != NULL) {
        text_body->lines[index].buf = strdup(tok);
        text_body->lines[index].len = (int) strlen(tok);
        text_body->len++;

        index++;
        tok = strtok(NULL, "\n");
    }
}

void print_text_body(const TextBody *text_body) {
    for (int i = 0; i < text_body->len; i++) {
        printf("%d: %s\n", i, text_body->lines[i].buf);
    }
}

char *mode_to_text() {
    switch (mode) {
        case NORMAL: return "NORMAL";
        case INSERT: return "INSERT";
        case VISUAL: return "VISUAL";
        default:
            exit(1);
    }
}

void render_ui() {
    clear_screen();

    // print mode to bottom left
    print_in_row_col(mode_to_text(), rows, 1);

    // print dims to bottom right
    char tmp[64];
    format_win_dims(tmp, sizeof(tmp), rows, cols);

    const int tmp_len = (int) strlen(tmp);

    print_in_row_col(tmp, rows, cols - tmp_len);
}

void render_text(const TextBody *text_body) {
    int curr_row = 1;
    int curr_col = 1;

    for (int i = 0; i < text_body->len; i++) {
        print_in_row_col(text_body->lines[i].buf, curr_row, curr_col);
        curr_row++;
    }
}

void render(const TextBody *text_body) {
    render_ui();
    render_text(text_body);
    move_to_row_col(cursor_row, cursor_col);
    fflush(stdout);
}

void handle_winch(int sig) {
    get_terminal_size();

    render_ui();
}

void handle_normal(const char c) {
    if (c == 'i') {
        mode = INSERT;
    }
    if (c == 'v') {
        mode = VISUAL;
    }
    if (c == 'j') {
        cursor_row++;
    }
    if (c == 'k' && cursor_row > 1) {
        cursor_row--;
    }
    if (c == 'l') {
        cursor_col++;
    }
    if (c == 'h' && cursor_col > 1) {
        cursor_col--;
    }
}

void handle_insert(const char c) {
    if (c == '\x1b') {
        mode = NORMAL;
    }
}

void handle_visual(const char c) {
    if (c == '\x1b') {
        mode = NORMAL;
    }
}

bool handle_input() {
    const char c = read_key();

    switch (mode) {
        case NORMAL:
            if (c == 'q') return false;
            handle_normal(c);
            break;
        case INSERT:
            handle_insert(c);
            break;
        case VISUAL:
            handle_visual(c);
            break;
    }

    return true;
}

int main(void) {
    atexit(disable_raw_mode);
    atexit(exit_alternate_screen);

    get_terminal_size();
    signal(SIGWINCH, handle_winch);

    enable_raw_mode();
    enter_alternate_screen();

    TextBody body;
    body.lines = malloc(1024 * sizeof(Line));
    body.len = 0;

    char text[] = "Some text.\nMultiple lines";
    populate_lines(&body, text);

    clear_screen();
    render(&body);

    bool running = true;

    while (running) {
        running = handle_input();
        render(&body);
    }

    free(body.lines);
    return 0;
}
