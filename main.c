#include <ctype.h>
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

// need to describe inputs as token
// so for example 'j' functions to move you down, so '5j' functions to move 5 down
// so 'j' is a terminating  character, and '5' is not, it's a modifier

typedef struct Token {
    char c;
    bool terminating;
} Token;

// rows and cols available on current window size
static int rows, cols;

// current line and char position
static int curr_line_num = 1;
static int curr_char_num = 1;

// body of text
TextBody body;

// normal buffer and its length
Token normal_buf[64];
int normal_buf_len = 0;

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

void render_normal_buf() {
    char debug_normal_buf[128] = "";
    for (int i = 0; i < normal_buf_len; i++) {
        debug_normal_buf[i] = normal_buf[i].c;
    }

    print_in_row_col(debug_normal_buf, rows - 1, 1);
}

void render_ui() {
    // print mode to bottom left
    print_in_row_col(mode_to_text(), rows, 1);

    // print dims to bottom right
    char tmp[64];
    format_win_dims(tmp, sizeof(tmp), rows, cols);

    const int tmp_len = (int) strlen(tmp);

    print_in_row_col(tmp, rows, cols - tmp_len);

    render_normal_buf();
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
    clear_screen();

    render_ui();
    render_text(text_body);
    move_to_row_col(curr_line_num, curr_char_num);
    fflush(stdout);
}

void handle_winch(int sig) {
    get_terminal_size();

    render_ui();
}

int min(const int x, const int y) {
    return (x < y) ? x : y;
}

// TODO: use rest of input buffer to affect movement by numbers
// I think it can be done by going back one token at a time, attempting to construct biggest pattern possible, ex 55j as input we can work back to construct 55 as a numeric type of 'motion', or with ciw as input we can construct ci as a motion, a change-type

void handle_normal_movement() {
    const Token terminating_token = normal_buf[normal_buf_len - 1];
    const char c = terminating_token.c;

    const int num_lines = body.len;

    const Line *curr_line = &body.lines[curr_line_num - 1];
    const int curr_line_len = curr_line->len;

    if (c == 'j' && curr_line_num < num_lines) {
        const Line *below_line = &body.lines[curr_line_num];
        const int below_line_len = below_line->len;
        curr_char_num = min(curr_char_num, below_line_len);

        curr_line_num++;
    }
    if (c == 'k' && curr_line_num > 1) {
        const Line *above_line = &body.lines[curr_line_num - 2];
        const int above_line_len = above_line->len;
        curr_char_num = min(curr_char_num, above_line_len);

        curr_line_num--;
    }
    if (c == 'l' && curr_char_num < curr_line_len) {
        curr_char_num++;
    }
    if (c == 'h' && curr_char_num > 1) {
        curr_char_num--;
    }
}

Token identify_token(const char c) {
    Token token;
    token.c = c;

    if (isdigit(c)) {
        token.terminating = false;
        return token;
    }

    switch (c) {
        case 'i':
        case 'v':
        case 'j':
        case 'k':
        case 'l':
        case 'h':
            token.terminating = true;
            break;
        case 'c':
            token.terminating = false;
            break;
        default:
            token.terminating = true;
            break;
    }

    return token;
}

void handle_normal_execution() {
    // last token was terminating
    const Token terminating_token = normal_buf[normal_buf_len - 1];

    switch (terminating_token.c) {
        case 'v':
            mode = VISUAL;
            break;
        case 'i':
            mode = INSERT;
            break;
        case 'j':
        case 'k':
        case 'h':
        case 'l':
            handle_normal_movement();
            break;
        default:
            break;
    }
}

void handle_normal_input(const char c) {
    const Token tk = identify_token(c);
    normal_buf[normal_buf_len] = tk;
    normal_buf_len++;

    if (tk.terminating) {
        handle_normal_execution();
        normal_buf_len = 0;
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
            handle_normal_input(c);
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

    body.lines = malloc(1024 * sizeof(Line));
    body.len = 0;

    char text[] = "Oh my lord this is the longest.\nSome.\nSome text.\nMultiple lines";
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
